#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <cstdint>
#include "types.h"
#include "bloom_filter.h"

struct IndexEntry { std::string key; int64_t offset; };

class SSTable {
public:
    static std::shared_ptr<SSTable> Serialize(
        const std::vector<std::shared_ptr<LSMEntry>>& entries,
        const std::string& filename);
    static std::shared_ptr<SSTable> Open(const std::string& filename);
    ~SSTable();

    std::shared_ptr<LSMEntry>              Get(const std::string& key);
    std::vector<std::shared_ptr<LSMEntry>> RangeScan(const std::string& startKey,
                                                      const std::string& endKey);
    std::vector<std::shared_ptr<LSMEntry>> GetEntries();
    const std::string& Filename() const { return filename_; }
    void Close();

private:
    SSTable() = default;
    BloomFilter*            bloom_filter_ = nullptr;
    std::vector<IndexEntry> index_;
    std::ifstream           file_;
    std::string             filename_;
    int64_t                 data_offset_ = 0;

    static int64_t                    WriteEntry(std::ofstream& f, const LSMEntry& e);
    static std::shared_ptr<LSMEntry>  ReadEntry(std::ifstream& f);
    static void                       WriteIndex(std::ofstream& f, const std::vector<IndexEntry>& idx);
    static std::vector<IndexEntry>    ReadIndex(std::ifstream& f);
    int64_t FindOffsetForKey(const std::string& key) const;
    int64_t FindStartOffsetForRangeScan(const std::string& startKey) const;
};
