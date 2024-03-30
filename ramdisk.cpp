/*

  ramdisk

  A simple contract that allows the storage of arbitrary amounts of binary data
  into an Antelope blockchain's RAM.

  Users ("owner") can store sparse files, which are arbitrary mappings of data
  node IDs (an uint64_t) to data (an array of bytes), and associate each sparse
  file with an Antelope 64-bit name. File names are first-come, first-serve.

  Once a file is created by its owner account, the owner can create and delete
  its data nodes. Files can be set to immutable.

  Notes:

  A good node size limit is 64,000 bytes, given that some Linux systems have 128kb
  command-line limits. You will need to post the entire data for a node on the
  command line as a hexadecimal text string when using cleos, which will bloat it
  to 128,000 bytes, leaving a good room of 3,072 bytes for the rest of the cleos
  command-line content.

  In any case, too-large blocks on the network are kind of bad, and the overhead
  of splitting at 64KB or e.g. 1MB (a common Antelope network transaction size
  limit in 2023) is roughtly the same.

*/

#include <eosio/eosio.hpp>

using namespace eosio;

using namespace std;

class [[eosio::contract]] ramdisk : public contract {
public:
  using contract::contract;

  // File table is scoped by file name, record is a singleton.
  struct [[eosio::table]] file {
    name                    owner;      // account that controls the file (0 == no one / immutable)
    uint64_t primary_key() const { return 0; }
  };

  typedef eosio::multi_index< "files"_n, file > files;

  // Node table is scoped by file name, record indexed by node id.
  struct [[eosio::table]] node {
    uint64_t                id;
    vector<unsigned char>   data;
    uint64_t primary_key() const { return id; }
  };

  typedef eosio::multi_index< "nodes"_n, node > nodes;

  /*
    Create a new file.

    Short names are free to create by anyone if the name has never been bid on.
    Filenames with dots (actual dots, not invisible trailing dots that short names have)
      and short names that have ever been bid on can only be created by the bid winner
      or the account whose name is the suffix.
   */
  [[eosio::action]]
  void create( name owner, name filename ) {
    uint8_t fnlen = filename.length();
    check( fnlen >= 1 && fnlen <= 12 , "Invalid filename." );
    require_auth( owner );

    // File must be new.
    files fls( _self, filename.value );
    auto pit = fls.begin();
    check( pit == fls.end(), "File exists." );

    // Filename authorization check.
    auto suffix = filename.suffix();
    bool is_short = fnlen < 12;
    if ( suffix != filename || is_short ) { // Must check name if either dotted or short.
      name_bid_table bids(SYSTEM_CONTRACT, SYSTEM_CONTRACT.value);
      auto current = bids.find( suffix.value );
      if ( current != bids.end() ) {
        check( current->high_bid < 0, "Suffix auction open." );
        check( current->high_bidder == owner, "Suffix winning bid not owned." );
      } else {
        // Bid doesn't exist on the name. If you own (i.e. are) the name, it's fine. If you don't, then
        //   you can still be fine if it's an undotted name of an account that doesn't exist yet.
        check( owner == suffix || (suffix == filename && !is_account(suffix) ), "Suffix account not owned." );
      }
    }

    // Create file.
    fls.emplace( owner, [&]( auto& p ) {
      p.owner = owner;
    });
  }

  /*
    Reset file data.
   */
  [[eosio::action]]
  void reset( name owner, name filename ) {
    files fls( _self, filename.value );
    auth_and_find_file( owner, filename, fls );
    clear_nodes( filename );
  }

  /*
    Delete file.
  */
  [[eosio::action]]
  void del( name owner, name filename ) {
    files fls( _self, filename.value );
    files::const_iterator pit = auth_and_find_file( owner, filename, fls );
    fls.erase( pit );
    clear_nodes( filename );
  }

  /*
    Set file to immutable (set owner to invalid account name).
   */
  [[eosio::action]]
  void setimmutable( name owner, name filename ) {
    files fls( _self, filename.value );
    files::const_iterator pit = auth_and_find_file( owner, filename, fls );
    fls.modify( pit, same_payer, [&]( auto& p ) {
      p.owner = ""_n; // should be impossible to create an account with the empty name
    });
  }

  /*
    Assign data to a node of an existing file.
  */
  [[eosio::action]]
  void setnode( name owner, name filename, uint64_t nodeid, vector<unsigned char> nodedata ) {
    files fls( _self, filename.value );
    auth_and_find_file( owner, filename, fls );
    nodes nds( _self, filename.value );
    auto nit = nds.find( nodeid );
    if (nit == nds.end()) {
      nds.emplace( owner, [&]( auto& n ) {
	n.id = nodeid;
	n.data = nodedata;
      });
    } else {
      nds.modify( nit, same_payer, [&]( auto& n ) {
	n.data = nodedata;
      });
    }
  }

  /*
    Delete a node of an existing file.
   */
  [[eosio::action]]
    void delnode( name owner, name filename, uint64_t nodeid ) {
    files fls( _self, filename.value );
    auth_and_find_file( owner, filename, fls );
    nodes nds( _self, filename.value );
    auto nit = nds.find( nodeid );
    if (nit != nds.end()) {
      nds.erase( nit );
    }
  }

  /*
    Delete a node range (startid, endid) of an existing file.
  */
  [[eosio::action]]
    void delnodes( name owner, name filename, uint64_t startid, uint64_t endid) {
    files fls( _self, filename.value );
    auth_and_find_file( owner, filename, fls );
    nodes nds( _self, filename.value );
    auto nit = nds.find( startid );
    while (nit != nds.end() && nit->id <= endid) {
      nit = nds.erase( nit );
    }
  }

  /*
    Delete a node range of an existing file, given the first id and a node count.
    Node IDs in the given range must be contiguous.
  */
  [[eosio::action]]
    void delnodec( name owner, name filename, uint64_t startid, uint64_t count) {
    files fls( _self, filename.value );
    auth_and_find_file( owner, filename, fls );
    nodes nds( _self, filename.value );
    auto nit = nds.find( startid );
    uint64_t expected = startid;
    while (nit != nds.end() && nit->id == expected) {
      nit = nds.erase( nit );
      ++expected;
    }
  }

private:

  // Expected name table from the system contract deployed at the system account.
  // If your blockchain has different parameters for this, you must set it here.
  static constexpr name SYSTEM_CONTRACT = "eosio"_n;
  struct name_bid {
    name            newname;
    name            high_bidder;
    int64_t         high_bid;
    time_point      last_bid_time;
    uint64_t primary_key()const { return newname.value;                    }
    uint64_t by_high_bid()const { return static_cast<uint64_t>(-high_bid); }
  };
  typedef eosio::multi_index< "namebids"_n, name_bid,
    indexed_by<"highbid"_n, const_mem_fun<name_bid, uint64_t, &name_bid::by_high_bid> >
    > name_bid_table;

  void clear_nodes( name filename ) {
    nodes nds( _self, filename.value );
    auto nit = nds.begin();
    while ( nit != nds.end() ) {
      nds.erase(nit++);
    }
  }

  files::const_iterator auth_and_find_file( name owner, name filename, const files & fls ) {
    require_auth( owner );
    files::const_iterator pit = fls.begin();
    check( pit != fls.end(), "File does not exist." );
    check( pit->owner == owner, "Not file owner." );
    return pit;
  }

};
