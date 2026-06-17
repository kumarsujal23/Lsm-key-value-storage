#include "lsm.h"
#include "merge_utils.h"
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <sstream>

namespace fs = std::filesystem;

// ════════════════════════════════════════════════════════════
//  OPEN / CLOSE
// ════════════════════════════════════════════════════════════

// Equivalent of func Open() in Go, minus the goroutines.
std::shared_ptr<LSMTree> LSMTree::Open(const std::string& directory,
                                        int64_t max_memtable_size,
                                        bool recover) {
    auto lsm = std::shared_ptr<LSMTree>(new LSMTree());
    lsm->directory_         = directory;
    lsm->max_memtable_size_ = max_memtable_size;
    lsm->memtable_          = std::make_unique<Memtable>();

    fs::create_directories(directory);

    // One WAL file for the database's lifetime. It's wiped (not deleted)
    // every time the active memtable is flushed.
    lsm->wal_ = std::make_unique<WAL>(directory + "/wal.log");

    // Load any SSTables left over from a previous run.
    lsm->LoadSSTables();

    // Replay any WAL entries that never made it into an SSTable before
    // the last shutdown/crash.
    if (recover) lsm->RecoverFromWAL();

    return lsm;
}

LSMTree::~LSMTree() { Close(); }

// Equivalent of func (l *LSMTree) Close() in Go.
// Flush whatever's left in the memtable so nothing is lost, then close
// the WAL handle.
void LSMTree::Close() {
    if (memtable_ && memtable_->Len() > 0) {
        sst_sequence_++;
        std::string filename = GetSSTableFilename(0);
        auto sst = SSTable::Serialize(memtable_->GetEntries(), filename);

        levels_[0].push_back(sst);
        wal_->Clear();
        memtable_->Clear();

        CompactLevelIfNeeded(0);
    }

    if (wal_) wal_->Close();
}

// ════════════════════════════════════════════════════════════
//  PUBLIC API
// ════════════════════════════════════════════════════════════

// Equivalent of func (l *LSMTree) Put() in Go.
// 1. Write to WAL (durability)
// 2. Write to memtable
// 3. Flush immediately if the memtable is now too big
void LSMTree::Put(const std::string& key, const std::string& value) {
    if (!in_recovery_) {
        int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        wal_->WriteEntry(key, value, Command::PUT, ts);
    }

    memtable_->Put(key, value);
    FlushMemtableIfNeeded();
}

// Equivalent of func (l *LSMTree) Delete() in Go.
void LSMTree::Delete(const std::string& key) {
    if (!in_recovery_) {
        int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        wal_->WriteEntry(key, "", Command::DELETE, ts);
    }

    memtable_->Delete(key);
    FlushMemtableIfNeeded();
}

// Equivalent of func (l *LSMTree) Get() in Go, minus the flushing-queue
// step — with synchronous flushing there's no in-between state to check;
// data is either still in the memtable or already safely in an SSTable.
// Search order: memtable → L0 → L1 → ... newest SSTable first per level.
std::string LSMTree::Get(const std::string& key) {
    auto entry = memtable_->Get(key);
    if (entry) {
        if (entry->command == Command::DELETE) return "";
        return entry->value;
    }

    for (int lvl = 0; lvl < MAX_LEVELS; lvl++) {
        for (int i = static_cast<int>(levels_[lvl].size()) - 1; i >= 0; i--) {
            auto e = levels_[lvl][i]->Get(key);
            if (e) {
                if (e->command == Command::DELETE) return "";
                return e->value;
            }
        }
    }
    return "";  // not found anywhere
}

// Equivalent of func (l *LSMTree) RangeScan() in Go.
// Collect ranges from memtable + every SSTable, then merge/deduplicate.
std::vector<std::pair<std::string,std::string>> LSMTree::RangeScan(
    const std::string& start_key, const std::string& end_key) {

    std::vector<std::vector<std::shared_ptr<LSMEntry>>> ranges;
    ranges.push_back(memtable_->RangeScan(start_key, end_key));

    for (int lvl = 0; lvl < MAX_LEVELS; lvl++) {
        for (int i = static_cast<int>(levels_[lvl].size()) - 1; i >= 0; i--) {
            ranges.push_back(levels_[lvl][i]->RangeScan(start_key, end_key));
        }
    }

    auto merged = MergeRanges(ranges);

    std::vector<std::pair<std::string,std::string>> results;
    for (const auto& entry : merged) results.push_back({entry->key, entry->value});
    return results;
}

// ════════════════════════════════════════════════════════════
//  FLUSHING — synchronous, in-line
// ════════════════════════════════════════════════════════════

// Called after every Put/Delete. If the memtable now exceeds the
// threshold, flush it RIGHT HERE before returning control to the caller.
// No background thread, no queue — the caller simply waits slightly
// longer on the write that happens to cross the threshold.
void LSMTree::FlushMemtableIfNeeded() {
    if (memtable_->SizeInBytes() <= max_memtable_size_) return;

    sst_sequence_++;
    std::string filename = GetSSTableFilename(0);

    // Write everything currently in the memtable to a new SSTable at L0.
    auto sst = SSTable::Serialize(memtable_->GetEntries(), filename);
    levels_[0].push_back(sst);

    // The memtable's data is now durable on disk — the WAL entries
    // protecting it are no longer needed. Wipe the WAL clean.
    wal_->Clear();

    // Memtable goes back to empty, ready for new writes.
    memtable_->Clear();

    // L0 might now have too many SSTables — check and compact if so.
    CompactLevelIfNeeded(0);
}

// ════════════════════════════════════════════════════════════
//  COMPACTION — synchronous, recursive
// ════════════════════════════════════════════════════════════

// Equivalent of func (l *LSMTree) compactLevel() in Go, called in-line
// instead of via a background goroutine + channel.
//
// If `level` has reached its SSTable limit, merge all of them into one
// SSTable at level+1, delete the old files, then recursively check
// level+1 in case THAT now needs compacting too (cascading compaction).
void LSMTree::CompactLevelIfNeeded(int level) {
    if (level >= MAX_LEVELS - 1) return;
    if (static_cast<int>(levels_[level].size()) < MAX_LEVEL_SSTABLES[level]) return;

    // Gather every entry from every SSTable at this level.
    std::vector<std::vector<std::shared_ptr<LSMEntry>>> ranges;
    std::vector<std::string> old_filenames;
    for (const auto& sst : levels_[level]) {
        ranges.push_back(sst->GetEntries());
        old_filenames.push_back(sst->Filename());
    }

    // k-way merge: dedupes by key (newest timestamp wins), drops tombstones.
    auto merged = MergeRanges(ranges);

    levels_[level].clear();
    for (const auto& fname : old_filenames) fs::remove(fname);

    if (merged.empty()) return;  // everything was tombstoned away

    sst_sequence_++;
    std::string new_filename = GetSSTableFilename(level + 1);
    auto new_sst = SSTable::Serialize(merged, new_filename);
    levels_[level + 1].push_back(new_sst);

    // The merge might have pushed level+1 over its own limit — check.
    CompactLevelIfNeeded(level + 1);
}

// ════════════════════════════════════════════════════════════
//  STARTUP: LOAD SSTABLES + WAL RECOVERY
// ════════════════════════════════════════════════════════════

// Equivalent of func (l *LSMTree) loadSSTablesFromDisk() in Go.
// Scans the directory for sstable_* files, sorts them by sequence number
// (oldest first), and opens each one.
void LSMTree::LoadSSTables() {
    if (!fs::exists(directory_)) return;

    std::vector<std::pair<uint64_t, std::pair<int, std::string>>> sst_files;

    for (const auto& entry : fs::directory_iterator(directory_)) {
        if (entry.is_directory()) continue;
        std::string fname = entry.path().filename().string();
        if (fname.substr(0, SST_PREFIX.size()) != SST_PREFIX) continue;

        int      lvl = GetLevelFromFilename(fname);
        uint64_t seq = GetSequenceFromFilename(fname);
        sst_files.push_back({seq, {lvl, entry.path().string()}});
    }

    std::sort(sst_files.begin(), sst_files.end());

    for (const auto& [seq, lf] : sst_files) {
        auto [lvl, path] = lf;
        auto sst = SSTable::Open(path);
        levels_[lvl].push_back(sst);
        if (seq > sst_sequence_) sst_sequence_ = seq;
    }
}

// Equivalent of func (l *LSMTree) recoverFromWAL() in Go.
// Replays every entry still sitting in the WAL — since Clear() wipes it
// on every flush, this is always at most one memtable's worth of writes.
void LSMTree::RecoverFromWAL() {
    in_recovery_ = true;
    auto entries = wal_->RecoverEntries();

    for (const auto& entry : entries) {
        if (entry->command == Command::PUT) {
            Put(entry->key, entry->value);
        } else if (entry->command == Command::DELETE) {
            Delete(entry->key);
        }
    }

    in_recovery_ = false;
}

// ════════════════════════════════════════════════════════════
//  FILENAME HELPERS
// ════════════════════════════════════════════════════════════

// Format: {directory}/sstable_{level}_{sequence}
// Example: data/sstable_0_42
std::string LSMTree::GetSSTableFilename(int level) {
    std::ostringstream ss;
    ss << directory_ << "/" << SST_PREFIX << level << "_" << sst_sequence_;
    return ss.str();
}

// "sstable_2_42" → 2
int LSMTree::GetLevelFromFilename(const std::string& filename) {
    size_t prefix_end = SST_PREFIX.size();
    return std::stoi(filename.substr(prefix_end, 1));
}

// "sstable_2_42" → 42
uint64_t LSMTree::GetSequenceFromFilename(const std::string& filename) {
    size_t underscore = filename.rfind('_');
    return std::stoull(filename.substr(underscore + 1));
}
