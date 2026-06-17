#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include "types.h"

// Write-Ahead Log — simple, synchronous version.
//
// Every Put/Delete is appended here BEFORE touching the memtable, so a
// crash mid-write can still be recovered by replaying the WAL.
//
// Because flushing is synchronous (happens immediately, in-line, the
// moment the memtable crosses the size threshold — no background thread,
// no queue), the WAL only ever holds entries for the CURRENTLY active
// memtable. The instant that memtable is flushed to an SSTable, every
// entry in the WAL is now safely on disk and worthless — so we just
// wipe the file empty with Clear() and start over.
//
// This means the WAL never grows unbounded and recovery never has to
// read more than "one memtable's worth" of data, regardless of how long
// the database has been running.
//
// File format: sequence of entries, each:
//   [key_len:4][key][value_len:4][value][command:1][timestamp:8]
class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    // Appends one entry. Called before every Put/Delete.
    void WriteEntry(const std::string& key,
                    const std::string& value,
                    Command command,
                    int64_t timestamp);

    // Reads every entry currently in the WAL, in order.
    // Safe to load fully into RAM — the WAL is always small because it's
    // wiped on every flush.
    std::vector<std::shared_ptr<LSMEntry>> RecoverEntries();

    // Wipes the WAL file empty. Called right after a successful flush —
    // every entry that was in here is now durable inside an SSTable.
    void Clear();

    void Close();

private:
    std::string   path_;
    std::ofstream write_file_;  // append-only handle, reopened by Clear()
};
