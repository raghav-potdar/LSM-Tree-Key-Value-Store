#pragma once
#include "memtable.hpp"
#include "bloom_filter.hpp"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <cstdint>

// An immutable, sorted file on disk.
//
// File layout:
//   [Data section]   sequence of records, sorted by key ascending
//   [Index section]  sparse index: every kIndexInterval-th key -> file offset
//   [Bloom section]  serialized BloomFilter bytes
//   [Footer]         fixed 40 bytes: index_offset, index_size,
//                                    bloom_offset, bloom_size, magic
//
// Record format in the data section:
//   uint8_t  is_tombstone
//   uint64_t seq_num
//   uint32_t key_len
//   bytes    key
//   uint32_t value_len   (0 if tombstone)
//   bytes    value       (absent if tombstone)
class SSTable {
public:
    static constexpr size_t kIndexInterval = 16;
    static constexpr uint64_t kMagic = 0x4C534D5353544142ULL; // "LSMSSTAB"

    static std::unique_ptr<SSTable> CreateFromEntries(
        const std::vector<MemTableEntry>& entries,
        const std::string& file_path,
        uint64_t level);

    static std::unique_ptr<SSTable> Open(const std::string& file_path);

    std::optional<std::string> Get(const std::string& key) const;

    // Tri-state version: distinguishes "not in this table" from "tombstoned
    // in this table" so the engine can stop searching older levels correctly.
    LookupResult Lookup(const std::string& key) const;

    // Full sorted scan of every entry (including tombstones) -- used by compaction.
    std::vector<MemTableEntry> GetAllEntries() const;

    uint64_t Level() const { return level_; }
    const std::string& FilePath() const { return file_path_; }
    const std::string& MinKey() const { return min_key_; }
    const std::string& MaxKey() const { return max_key_; }

    bool OverlapsRange(const std::string& key_lo, const std::string& key_hi) const;

private:
    struct Record {
        bool is_tombstone;
        uint64_t seq_num;
        std::string key;
        std::string value;
    };

    std::string file_path_;
    uint64_t level_ = 0;
    uint64_t index_offset_ = 0;
    std::string min_key_, max_key_;
    BloomFilter bloom_filter_;
    std::vector<std::pair<std::string, uint64_t>> sparse_index_;

    static bool ReadRecordAt(std::ifstream& in, uint64_t offset, Record* out);
    static void WriteRecord(std::ofstream& out, const MemTableEntry& e);
};