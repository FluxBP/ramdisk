# ramdisk

A contract that provides a simple RAM-based file system for Antelope

# Rationale

This contract is a generalization of what [PermaStore](https://github.com/FluxBP/pstore) does.

Whereas PermaStore forces Antelope names ("file names") to be mapped to contiguous data blocks, which makes it useful for associating individual data files with Antelope names, ramdisk allows any `uint64_t` node id to be assigned, in any order, thus allowing for sparse files.

A sparse file is, in essence, a disk. This allows for more sophisticated use cases, where an user is really registering a disk name, and managing each namespace as a disk which can store multiple files independently.
