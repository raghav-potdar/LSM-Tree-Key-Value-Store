#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Standard bit-array bloom filter with double hashing (two base hashes
// combined to simulate k independent hash functions -- avoids needing
// k separate hash implementations).
class BloomFilter {
public:
    BloomFilter() = default;
    BloomFilter(size_t num_keys, double false_positive_rate);

    void Add(const std::string& key);
    bool MightContain(const std::string& key) const; // false = definitely absent

    std::vector<uint8_t> Serialize() const;
    static BloomFilter Deserialize(const std::vector<uint8_t>& data);

private:
    size_t num_bits_ = 0;
    size_t num_hashes_ = 0;
    std::vector<uint8_t> bits_; // packed bits, 8 per byte

    void SetBit(size_t idx);
    bool GetBit(size_t idx) const;

    static uint64_t Hash1(const std::string& key);
    static uint64_t Hash2(const std::string& key);
};