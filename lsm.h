#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "types.h"
#include "memtable.h"
#include "sstable.h"
#include "wal.h"

// ── Constants ──────────────────────────────────────────────────────────────
static const std::string SST_PREFIX = "sstable_";
static const int         MAX_LEVELS = 6;

// Max SSTables allowed per level before compaction merges them down a level.
static const int MAX_LEVEL_SSTABLES[MAX_LEVELS] = {4, 8, 16, 32, 64, 128};

// ── LSMTree ────────────────────────────────────────────────────────────────
// The main engine, tying together Memtable, WAL, and SSTables.
//
// Deliberately single-threaded and synchronous: every Put/Delete that
// crosses the memtable size threshold flushes immediately, in-line,
// before the call returns. No background threads, no flush queue, no
// locks. This trades away concurrent throughput for something far more
// important for this project: every operation has one clear, traceable
// code path — which is what actually matters when explaining it.
class LSMTree {
public:
    // Opens (or creates) the engine at the given directory.
    // max_memtable_size: flush threshold in bytes.
    // recover: if true, replay the WAL on startup (crash recovery).
    static std::shared_ptr<LSMTree> Open(const std::string& directory,
                                          int64_t max_memtable_size,
                                          bool recover);

    ~LSMTree();
    void Close();

    void        Put(const std::string& key, const std::string& value);
    void        Delete(const std::string& key);
    std::string Get(const std::string& key);  // returns "" if not found
    std::vector<std::pair<std::string,std::string>> RangeScan(
        const std::string& start_key, const std::string& end_key);

private:
    LSMTree() = default;  // use Open() to construct

    std::string               directory_;
    int64_t                    max_memtable_size_;
    std::unique_ptr<Memtable>  memtable_;
    std::unique_ptr<WAL>       wal_;
    bool                       in_recovery_ = false;
    uint64_t                   sst_sequence_ = 0;

    // One vector of SSTables per level. levels_[0] is L0, levels_[5] is
    // the deepest level. No mutex needed — single-threaded by design.
    std::vector<std::shared_ptr<SSTable>> levels_[MAX_LEVELS];

    void LoadSSTables();
    void RecoverFromWAL();

    // Called after every Put/Delete. If the memtable now exceeds the
    // threshold, flush it to an SSTable immediately, right here, before
    // control returns to the caller.
    void FlushMemtableIfNeeded();

    // Checks if `level` has too many SSTables; if so, merges them all
    // into one SSTable at level+1 and recursively checks the next level.
    void CompactLevelIfNeeded(int level);

    std::string GetSSTableFilename(int level);
    int         GetLevelFromFilename(const std::string& filename);
    uint64_t    GetSequenceFromFilename(const std::string& filename);
};
