#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "types.h"

static const int   MAX_LEVEL        = 16;
static const float SKIP_PROBABILITY = 0.5f;

struct SkipNode {
    std::string                       key;
    std::shared_ptr<LSMEntry>         entry;
    std::vector<SkipNode*>            forward;
    SkipNode(int level, const std::string& k, std::shared_ptr<LSMEntry> e)
        : key(k), entry(e), forward(level + 1, nullptr) {}
};

class SkipList {
public:
    SkipList();
    ~SkipList();
    void                      Set(const std::string& key, std::shared_ptr<LSMEntry> entry);
    std::shared_ptr<LSMEntry> Get(const std::string& key) const;
    SkipNode*                 Find(const std::string& key) const;
    SkipNode*                 Front() const;
    int                       Len() const;
    void                      Init();
private:
    int       randomLevel();
    SkipNode* head_;
    int       currentLevel_;
    int       len_;
};

class Memtable {
public:
    Memtable();
    void                      Put(const std::string& key, const std::string& value);
    void                      Delete(const std::string& key);
    std::shared_ptr<LSMEntry> Get(const std::string& key) const;
    std::vector<std::shared_ptr<LSMEntry>> RangeScan(const std::string& startKey,
                                                      const std::string& endKey) const;
    std::vector<std::shared_ptr<LSMEntry>> GetEntries() const;
    int64_t SizeInBytes() const;
    int     Len() const;
    void    Clear();
private:
    SkipList data_;
    int64_t  size_;
    static std::shared_ptr<LSMEntry> MakeLSMEntry(const std::string& key,
                                                   const std::string& value,
                                                   Command command);
};
