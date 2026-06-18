# Log-Structured Merge-Tree Storage Engine

A key-value storage engine built from scratch in C++17, modeled after the internals of LevelDB and RocksDB. Supports `Put`, `Get`, `Delete`, and `RangeScan` operations with full crash recovery via a Write-Ahead Log.

---

## Architecture

The core insight behind LSM trees: **never do random writes to disk**. Every write first lands in RAM (the memtable), gets appended to a log file (WAL) for crash safety, and is eventually flushed to an immutable sorted file on disk (SSTable) in one sequential write. This eliminates the random disk I/O that makes B-tree based engines slow under write-heavy workloads.

```
Write path:
  Put(key, value)
    → append to WAL        (crash durability — sequential write)
    → insert into memtable (sorted skip list in RAM)
    → if memtable full:
        flush to SSTable   (one sequential disk write)
        clear WAL          (data is now safe on disk)
        compact if needed  (merge SSTables down levels)

Read path:
  Get(key)
    → check memtable       (RAM, O(log n) skip list lookup)
    → check L0 SSTables (newest first):
        bloom filter       (skip if key definitely absent)
        binary search index (offset into file, both in RAM)
        seek + read        (one disk read)
    → repeat for L1, L2 ... until found or exhausted
```

---

## Components

### `types.h`
Defines `LSMEntry` (the unit of data) and the `Command` enum (`PUT`/`DELETE`). Shared across every other file.

### `murmur3.h`
Header-only C++ port of Austin Appleby's MurmurHash3 algorithm. Provides `murmur3_sum64(key)` and `murmur3_sum64_with_seed(key, seed)` — the two hash functions the bloom filter needs.

### `bloom_filter.h / .cpp`
A Bloom filter backed by a `vector<bool>` bitset. On `Add(key)`, hashes the key three times with different seeds and flips three bits true. On `Test(key)`, checks if all three bits are still true — if any is false, the key is definitely absent (no disk read needed). False positives are possible; false negatives are impossible.

Each SSTable has its own Bloom filter stored at the front of the file, loaded into RAM when the SSTable is opened.

### `memtable.h / .cpp`
The in-memory write buffer. Backed by a **skip list** implemented from scratch — a sorted linked list with multiple "express lane" levels of forward pointers, giving O(log n) insert and search while keeping data sorted at all times. The sorted order is the key property: flushing to an SSTable is just a sequential walk of the skip list, requiring no additional sorting step.

### `sstable.h / .cpp`
One immutable sorted file on disk. File format:

```
[ bloom filter size : 8 bytes ]
[ bloom filter bits : N bytes ]  ← one byte per bit
[ index entry count : 8 bytes ]
[ index entries     : M bytes ]  ← (key_len, key, byte_offset) per entry
[ entries           : rest     ]  ← (key_len, key, val_len, val, command, timestamp) per entry
```

The bloom filter and index are loaded into RAM on open. The entries section stays on disk — a `Get` does a binary search on the in-memory index to find the exact byte offset, then one `seekg` + read.

### `merge_utils.h / .cpp`
K-way merge using a min-heap (`std::priority_queue`). Takes multiple sorted entry lists (one per SSTable being compacted), merges them while keeping only the most recent version of each key (by timestamp), and discards tombstones. The output is a single sorted, deduplicated list ready to be written as a new SSTable.

### `wal.h / .cpp`
Write-Ahead Log — an append-only file that every `Put`/`Delete` writes to **before** touching the memtable. On restart, any entries still in the WAL (that were never flushed to an SSTable) are replayed to rebuild the memtable.

Because flushing is synchronous, the WAL is **wiped clean immediately after every flush** — it never holds more than one memtable's worth of entries. Recovery is always bounded and fast regardless of how long the database has been running.

### `lsm.h / .cpp`
The main engine. Owns the memtable, WAL, and all SSTable levels. Coordinates the full write path (WAL → memtable → flush → compact) and read path (memtable → L0 → L1 → ...). Deliberately **single-threaded and synchronous** — flush and compaction happen in-line on the calling thread, with no background threads or locks. Every operation has one clear code path.

---

## Compaction

SSTables are organized into levels (L0 through L5). Each level has a maximum SSTable count:

```
L0: 4   L1: 8   L2: 16   L3: 32   L4: 64   L5: 128
```

New SSTables always land at L0 (from memtable flushes). When a level reaches its limit, all SSTables at that level are merged into one SSTable at the next level using k-way merge, old files are deleted, and the process checks the next level recursively. This keeps the number of files bounded and prevents read performance from degrading as data accumulates.

---

## Build

```bash
mkdir build && cd build
cmake ..
make
```

Requires: C++17 compiler, CMake 3.16+.

---

## Usage

```bash
./lsm_engine <directory>       # opens or creates a database at <directory>
./lsm_engine ./mydata          # example
```

Commands:

```
put <key> <value>          Insert or update a key
get <key>                  Retrieve a value (returns nil if not found)
delete <key>               Delete a key (writes a tombstone)
scan <startKey> <endKey>   Range scan, inclusive on both ends
exit                       Flush remaining data and exit cleanly
```

Example session:

```
> put alice 100
OK
> put bob 200
OK
> get alice
100
> scan alice bob
alice → 100
bob → 200
> delete alice
OK
> get alice
(nil)
> exit
Closing engine...
Done.
```

Data persists across sessions. Reopening the same directory loads existing SSTables and replays the WAL if there are any unflushed entries from a previous crash.

---

## Design Decisions

**Skip list over BST for the memtable** — a BST insert can trigger rebalancing, which modifies multiple nodes and complicates concurrent access. A skip list insert only updates a small, localized set of forward pointers — no rebalancing, no cascading modifications. More importantly, it maintains sorted order on every insert, making SSTable flushes a simple O(n) traversal.

**LSM tree over B-tree** — B-tree updates are in-place random writes to specific nodes on disk, causing the disk head to seek on every write. LSM trees are append-only: every write is a sequential append to the WAL, and every flush is a single sequential write for the entire SSTable. For write-heavy workloads, this eliminates the primary bottleneck of disk-based storage.

**Custom binary serialization over protobuf** — entries are serialized as raw bytes in a fixed layout (`key_len`, `key`, `value_len`, `value`, `command`, `timestamp`). No external dependency, no generated code, and every byte on disk has an explicit known purpose.

**Triple Murmur3 hashing for bloom filters** — three independent hashes (via different seeds) minimize the false positive rate for a given bitset size. One hash produces too many collisions; more than three gives diminishing returns while increasing the chance that unrelated keys share bits.

**Synchronous flush and compaction** — flushing and compaction run on the calling thread rather than background threads. This trades away concurrent write throughput for a simpler, fully traceable code path. There are no shared mutable state problems, no condition variables, no lock hierarchies to reason about.

**WAL wiped on every flush** — rather than using checkpoint markers inside a growing WAL file, the WAL is truncated to zero bytes immediately after every successful flush. This bounds recovery time to replaying at most one memtable's worth of writes, regardless of database age.
