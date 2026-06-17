#include "sstable.h"
#include <stdexcept>

int64_t SSTable::WriteEntry(std::ofstream& f, const LSMEntry& e) {
    uint32_t kl = e.key.size(), vl = e.value.size();
    uint8_t  cmd = static_cast<uint8_t>(e.command);
    f.write(reinterpret_cast<const char*>(&kl), 4); f.write(e.key.data(), kl);
    f.write(reinterpret_cast<const char*>(&vl), 4); f.write(e.value.data(), vl);
    f.write(reinterpret_cast<const char*>(&cmd), 1);
    f.write(reinterpret_cast<const char*>(&e.timestamp), 8);
    return 4 + kl + 4 + vl + 1 + 8;
}

std::shared_ptr<LSMEntry> SSTable::ReadEntry(std::ifstream& f) {
    uint32_t kl;
    f.read(reinterpret_cast<char*>(&kl), 4);
    if (!f || f.gcount() == 0) return nullptr;
    auto e = std::make_shared<LSMEntry>();
    e->key.resize(kl); f.read(e->key.data(), kl);
    uint32_t vl; f.read(reinterpret_cast<char*>(&vl), 4);
    e->value.resize(vl); f.read(e->value.data(), vl);
    uint8_t cmd; f.read(reinterpret_cast<char*>(&cmd), 1);
    e->command = static_cast<Command>(cmd);
    f.read(reinterpret_cast<char*>(&e->timestamp), 8);
    return e;
}

void SSTable::WriteIndex(std::ofstream& f, const std::vector<IndexEntry>& idx) {
    int64_t count = idx.size();
    f.write(reinterpret_cast<const char*>(&count), 8);
    for (const auto& e : idx) {
        uint32_t kl = e.key.size();
        f.write(reinterpret_cast<const char*>(&kl), 4);
        f.write(e.key.data(), kl);
        f.write(reinterpret_cast<const char*>(&e.offset), 8);
    }
}

std::vector<IndexEntry> SSTable::ReadIndex(std::ifstream& f) {
    int64_t count; f.read(reinterpret_cast<char*>(&count), 8);
    std::vector<IndexEntry> idx; idx.reserve(count);
    for (int64_t i = 0; i < count; i++) {
        IndexEntry e; uint32_t kl;
        f.read(reinterpret_cast<char*>(&kl), 4);
        e.key.resize(kl); f.read(e.key.data(), kl);
        f.read(reinterpret_cast<char*>(&e.offset), 8);
        idx.push_back(std::move(e));
    }
    return idx;
}

std::shared_ptr<SSTable> SSTable::Serialize(
    const std::vector<std::shared_ptr<LSMEntry>>& entries,
    const std::string& filename) {

    BloomFilter bloom(1000000);
    std::vector<IndexEntry> index;
    std::string buf;
    int64_t offset = 0;

    for (const auto& e : entries) {
        index.push_back({e->key, offset});
        bloom.Add(e->key);
        uint32_t kl = e->key.size(), vl = e->value.size();
        uint8_t  cmd = static_cast<uint8_t>(e->command);
        buf.append(reinterpret_cast<const char*>(&kl), 4); buf.append(e->key);
        buf.append(reinterpret_cast<const char*>(&vl), 4); buf.append(e->value);
        buf.append(reinterpret_cast<const char*>(&cmd), 1);
        buf.append(reinterpret_cast<const char*>(&e->timestamp), 8);
        offset += 4 + kl + 4 + vl + 1 + 8;
    }

    std::ofstream f(filename, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot create SSTable: " + filename);

    bloom.WriteTo(f);
    WriteIndex(f, index);
    f.write(buf.data(), buf.size());
    f.close();

    // Calculate data_offset
    int64_t data_offset = 8 + bloom.Size();  // bloom section
    data_offset += 8;                          // index count
    for (const auto& e : index) data_offset += 4 + e.key.size() + 8;

    auto sst = std::shared_ptr<SSTable>(new SSTable());
    sst->filename_     = filename;
    sst->bloom_filter_ = new BloomFilter(bloom);
    sst->index_        = std::move(index);
    sst->data_offset_  = data_offset;
    sst->file_.open(filename, std::ios::binary);
    return sst;
}

std::shared_ptr<SSTable> SSTable::Open(const std::string& filename) {
    auto sst = std::shared_ptr<SSTable>(new SSTable());
    sst->filename_ = filename;
    sst->file_.open(filename, std::ios::binary);
    if (!sst->file_) throw std::runtime_error("Cannot open SSTable: " + filename);

    sst->bloom_filter_ = new BloomFilter(BloomFilter::ReadFrom(sst->file_));
    sst->index_        = ReadIndex(sst->file_);

    int64_t data_offset = 8 + sst->bloom_filter_->Size();
    data_offset += 8;
    for (const auto& e : sst->index_) data_offset += 4 + e.key.size() + 8;
    sst->data_offset_ = data_offset;
    return sst;
}

SSTable::~SSTable() { Close(); delete bloom_filter_; }
void SSTable::Close() { if (file_.is_open()) file_.close(); }

int64_t SSTable::FindOffsetForKey(const std::string& key) const {
    int lo = 0, hi = static_cast<int>(index_.size()) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (index_[mid].key == key)       return index_[mid].offset;
        else if (index_[mid].key < key)   lo = mid + 1;
        else                               hi = mid - 1;
    }
    return -1;
}

int64_t SSTable::FindStartOffsetForRangeScan(const std::string& startKey) const {
    int lo = 0, hi = static_cast<int>(index_.size()) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (index_[mid].key == startKey)     return index_[mid].offset;
        else if (index_[mid].key < startKey) lo = mid + 1;
        else                                  hi = mid - 1;
    }
    if (lo >= static_cast<int>(index_.size())) return -1;
    return index_[lo].offset;
}

std::shared_ptr<LSMEntry> SSTable::Get(const std::string& key) {
    if (!bloom_filter_->Test(key)) return nullptr;
    int64_t offset = FindOffsetForKey(key);
    if (offset == -1) return nullptr;
    file_.seekg(data_offset_ + offset, std::ios::beg);
    return ReadEntry(file_);
}

std::vector<std::shared_ptr<LSMEntry>> SSTable::RangeScan(
    const std::string& startKey, const std::string& endKey) {
    std::vector<std::shared_ptr<LSMEntry>> results;
    int64_t start = FindStartOffsetForRangeScan(startKey);
    if (start == -1) return results;
    file_.seekg(data_offset_ + start, std::ios::beg);
    while (file_) {
        auto e = ReadEntry(file_);
        if (!e) break;
        if (e->key > endKey) break;
        results.push_back(e);
    }
    return results;
}

std::vector<std::shared_ptr<LSMEntry>> SSTable::GetEntries() {
    std::vector<std::shared_ptr<LSMEntry>> results;
    file_.seekg(data_offset_, std::ios::beg);
    while (file_) {
        auto e = ReadEntry(file_);
        if (!e) break;
        results.push_back(e);
    }
    return results;
}
