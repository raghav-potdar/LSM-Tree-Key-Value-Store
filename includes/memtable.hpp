#pragma once

#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <optional>

struct MemTableEntry {
    std::string key;
    std::string value;
    bool is_tombstone;
    uint64_t seq_num;
};

class MemTable {
public:
    virtual ~MemTable() = default;

    virtual void Put(const std::string& key, const std::string& value, uint64_t seq_num) = 0;

    virtual std::optional<std::string> Get(const std::string& key) const = 0;

    virtual void Delete(const std::string& key, uint64_t seq_num) = 0;

    virtual std::vector<MemTableEntry> GetSortedEntries() const = 0;

    virtual size_t Count() const = 0;

    virtual size_t ApproximateSizeBytes() const = 0;
};