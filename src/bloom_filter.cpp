#include "bloom_filter.hpp"
#include <cmath>
#include <functional>
#include <stdexcept>
#include <cstring>

BloomFilter::BloomFilter(size_t num_keys, double false_positive_rate) {
    if (num_keys == 0) num_keys = 1;
    double m = -(static_cast<double>(num_keys) * std::log(false_positive_rate)) /
               (std::log(2.0) * std::log(2.0));
    num_bits_ = static_cast<size_t>(std::ceil(m));
    if (num_bits_ < 8) num_bits_ = 8;

    double k = (static_cast<double>(num_bits_) / num_keys) * std::log(2.0);
    num_hashes_ = static_cast<size_t>(std::round(k));
    if (num_hashes_ < 1) num_hashes_ = 1;
    if (num_hashes_ > 30) num_hashes_ = 30;

    bits_.assign((num_bits_ + 7) / 8, 0);
}

void BloomFilter::SetBit(size_t idx) {
    bits_[idx / 8] |= static_cast<uint8_t>(1u << (idx % 8));
}

bool BloomFilter::GetBit(size_t idx) const {
    return (bits_[idx / 8] & static_cast<uint8_t>(1u << (idx % 8))) != 0;
}

uint64_t BloomFilter::Hash1(const std::string& key) {
    return std::hash<std::string>{}(key);
}

uint64_t BloomFilter::Hash2(const std::string& key) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

void BloomFilter::Add(const std::string& key) {
    if (num_bits_ == 0) return;
    uint64_t h1 = Hash1(key);
    uint64_t h2 = Hash2(key);
    for (size_t i = 0; i < num_hashes_; ++i) {
        uint64_t combined = h1 + i * h2;
        SetBit(combined % num_bits_);
    }
}

bool BloomFilter::MightContain(const std::string& key) const {
    if (num_bits_ == 0) return true;
    uint64_t h1 = Hash1(key);
    uint64_t h2 = Hash2(key);
    for (size_t i = 0; i < num_hashes_; ++i) {
        uint64_t combined = h1 + i * h2;
        if (!GetBit(combined % num_bits_)) return false;
    }
    return true;
}

std::vector<uint8_t> BloomFilter::Serialize() const {
    std::vector<uint8_t> out;
    uint64_t nb = num_bits_;
    uint64_t nh = num_hashes_;
    out.resize(16 + bits_.size());
    std::memcpy(out.data(), &nb, 8);
    std::memcpy(out.data() + 8, &nh, 8);
    std::memcpy(out.data() + 16, bits_.data(), bits_.size());
    return out;
}

BloomFilter BloomFilter::Deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 16) throw std::runtime_error("BloomFilter::Deserialize: truncated data");
    BloomFilter bf;
    uint64_t nb, nh;
    std::memcpy(&nb, data.data(), 8);
    std::memcpy(&nh, data.data() + 8, 8);
    bf.num_bits_ = static_cast<size_t>(nb);
    bf.num_hashes_ = static_cast<size_t>(nh);
    bf.bits_.assign(data.begin() + 16, data.end());
    return bf;
}