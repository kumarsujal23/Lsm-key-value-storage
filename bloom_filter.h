#pragma once
#include <string>
#include <vector>
#include <fstream>
#include "murmur.h"

class BloomFilter {
public:
    explicit BloomFilter(int64_t size);
    BloomFilter(const BloomFilter&) = default;
    static BloomFilter ReadFrom(std::ifstream& file);
    void    Add(const std::string& key);
    bool    Test(const std::string& key) const;
    void    WriteTo(std::ofstream& file) const;
    int64_t Size() const { return size_; }
private:
    int64_t           size_;
    std::vector<bool> bitset_;
};
