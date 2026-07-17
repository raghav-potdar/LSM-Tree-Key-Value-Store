#pragma once
#include "memtable.hpp"
#include <map>
#include <mutex>

class StdMapMemTable : public MemTable {
public:
    void Put(const std::string& key, const std::string& value, uint64_t seq_num) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(key);
        if (it != table_.end()) {
            approximate_size_ -= it->second.value.size();
            it->second = Entry{value, false, seq_num};
            approximate_size_ += value.size();
        } else {
            table_[key] = Entry{value, false, seq_num};
            approximate_size_ += key.size() + value.size();
        }
    }

    void Delete(const std::string& key, uint64_t seq_num) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(key);
        if (it != table_.end()) {
            approximate_size_ -= it->second.value.size();
            it->second = Entry{"", true, seq_num};
        } else {
            table_[key] = Entry{"", true, seq_num};
            approximate_size_ += key.size();
        }
    }

    std::optional<std::string> Get(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(key);
        if (it == table_.end() || it->second.is_tombstone) {
            return std::nullopt;
        }
        return it->second.value;
    }

    LookupResult Lookup(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(key);
        if (it == table_.end()) return LookupResult{false, false, ""};
        if (it->second.is_tombstone) return LookupResult{true, true, ""};
        return LookupResult{true, false, it->second.value};
    }

    size_t ApproximateSizeBytes() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return approximate_size_;
    }

    std::vector<MemTableEntry> GetSortedEntries() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MemTableEntry> result;
        result.reserve(table_.size());
        for (const auto& [key, entry] : table_) {
            result.push_back({key, entry.value, entry.is_tombstone, entry.seq_num});
        }
        return result;
    }

    size_t Count() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return table_.size();
    }

private:
    struct Entry {
        std::string value;
        bool is_tombstone;
        uint64_t seq_num;
    };

    mutable std::mutex mutex_;
    std::map<std::string, Entry> table_;
    size_t approximate_size_ = 0;
};