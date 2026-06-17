#include "memtable.h"
#include <chrono>
#include <cstdlib>

SkipList::SkipList() : currentLevel_(0), len_(0) {
    head_ = new SkipNode(MAX_LEVEL, "", nullptr);
}
SkipList::~SkipList() {
    SkipNode* curr = head_->forward[0];
    while (curr) { SkipNode* next = curr->forward[0]; delete curr; curr = next; }
    delete head_;
}
int SkipList::randomLevel() {
    int level = 0;
    while (((float)rand() / RAND_MAX) < SKIP_PROBABILITY && level < MAX_LEVEL - 1) level++;
    return level;
}
void SkipList::Set(const std::string& key, std::shared_ptr<LSMEntry> entry) {
    std::vector<SkipNode*> update(MAX_LEVEL, nullptr);
    SkipNode* curr = head_;
    for (int i = currentLevel_; i >= 0; i--) {
        while (curr->forward[i] && curr->forward[i]->key < key) curr = curr->forward[i];
        update[i] = curr;
    }
    curr = curr->forward[0];
    if (curr && curr->key == key) { curr->entry = entry; return; }
    int newLevel = randomLevel();
    if (newLevel > currentLevel_) {
        for (int i = currentLevel_ + 1; i <= newLevel; i++) update[i] = head_;
        currentLevel_ = newLevel;
    }
    SkipNode* node = new SkipNode(newLevel, key, entry);
    for (int i = 0; i <= newLevel; i++) { node->forward[i] = update[i]->forward[i]; update[i]->forward[i] = node; }
    len_++;
}
std::shared_ptr<LSMEntry> SkipList::Get(const std::string& key) const {
    SkipNode* curr = head_;
    for (int i = currentLevel_; i >= 0; i--)
        while (curr->forward[i] && curr->forward[i]->key < key) curr = curr->forward[i];
    curr = curr->forward[0];
    if (curr && curr->key == key) return curr->entry;
    return nullptr;
}
SkipNode* SkipList::Find(const std::string& key) const {
    SkipNode* curr = head_;
    for (int i = currentLevel_; i >= 0; i--)
        while (curr->forward[i] && curr->forward[i]->key < key) curr = curr->forward[i];
    return curr->forward[0];
}
SkipNode* SkipList::Front() const { return head_->forward[0]; }
int       SkipList::Len()   const { return len_; }
void SkipList::Init() {
    SkipNode* curr = head_->forward[0];
    while (curr) { SkipNode* next = curr->forward[0]; delete curr; curr = next; }
    for (int i = 0; i <= currentLevel_; i++) head_->forward[i] = nullptr;
    currentLevel_ = 0; len_ = 0;
}

Memtable::Memtable() : size_(0) {}

std::shared_ptr<LSMEntry> Memtable::MakeLSMEntry(const std::string& key,
                                                   const std::string& value,
                                                   Command command) {
    auto e = std::make_shared<LSMEntry>();
    e->key = key; e->value = value; e->command = command;
    e->timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return e;
}

void Memtable::Put(const std::string& key, const std::string& value) {
    int64_t delta = static_cast<int64_t>(value.size());
    auto existing = data_.Get(key);
    if (existing) size_ -= static_cast<int64_t>(existing->value.size());
    else          delta += static_cast<int64_t>(key.size());
    data_.Set(key, MakeLSMEntry(key, value, Command::PUT));
    size_ += delta;
}

void Memtable::Delete(const std::string& key) {
    auto existing = data_.Get(key);
    if (existing) size_ -= static_cast<int64_t>(existing->value.size());
    else          size_ += static_cast<int64_t>(key.size());
    data_.Set(key, MakeLSMEntry(key, "", Command::DELETE));
}

std::shared_ptr<LSMEntry> Memtable::Get(const std::string& key) const {
    return data_.Get(key);
}

std::vector<std::shared_ptr<LSMEntry>> Memtable::RangeScan(
    const std::string& startKey, const std::string& endKey) const {
    std::vector<std::shared_ptr<LSMEntry>> results;
    SkipNode* node = data_.Find(startKey);
    while (node && node->key <= endKey) { results.push_back(node->entry); node = node->forward[0]; }
    return results;
}

std::vector<std::shared_ptr<LSMEntry>> Memtable::GetEntries() const {
    std::vector<std::shared_ptr<LSMEntry>> results;
    SkipNode* node = data_.Front();
    while (node) { results.push_back(node->entry); node = node->forward[0]; }
    return results;
}

int64_t Memtable::SizeInBytes() const { return size_; }
int     Memtable::Len()         const { return data_.Len(); }
void    Memtable::Clear()             { data_.Init(); size_ = 0; }
