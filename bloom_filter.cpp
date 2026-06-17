#include "bloom_filter.h"
#include <stdexcept>

BloomFilter::BloomFilter(int64_t size) : size_(size), bitset_(size, false) {}

void BloomFilter::Add(const std::string& key) {
    uint64_t sz = static_cast<uint64_t>(size_);
    bitset_[murmur3_sum64(key)              % sz] = true;
    bitset_[murmur3_sum64_with_seed(key, 1) % sz] = true;
    bitset_[murmur3_sum64_with_seed(key, 2) % sz] = true;
}

bool BloomFilter::Test(const std::string& key) const {
    uint64_t sz = static_cast<uint64_t>(size_);
    return bitset_[murmur3_sum64(key)              % sz] &&
           bitset_[murmur3_sum64_with_seed(key, 1) % sz] &&
           bitset_[murmur3_sum64_with_seed(key, 2) % sz];
}

void BloomFilter::WriteTo(std::ofstream& file) const {
    file.write(reinterpret_cast<const char*>(&size_), sizeof(int64_t));
    for (int64_t i = 0; i < size_; i++) {
        uint8_t bit = bitset_[i] ? 1 : 0;
        file.write(reinterpret_cast<const char*>(&bit), sizeof(uint8_t));
    }
}

BloomFilter BloomFilter::ReadFrom(std::ifstream& file) {
    int64_t size;
    file.read(reinterpret_cast<char*>(&size), sizeof(int64_t));
    if (!file) throw std::runtime_error("Failed to read bloom filter size");
    BloomFilter bf(size);
    for (int64_t i = 0; i < size; i++) {
        uint8_t bit;
        file.read(reinterpret_cast<char*>(&bit), sizeof(uint8_t));
        bf.bitset_[i] = (bit == 1);
    }
    return bf;
}
