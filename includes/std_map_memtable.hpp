#ifndef STD_MAP_MEMTABLE_HPP
#define STD_MAP_MEMTABLE_HPP

#include "memtable.hpp"
#include <map>
#include <mutex>

class StdMapMemTable : public MemTable {
public:
    void Put(const std::string& key, const std::string& value, uint64_t seq_num) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(key);

        if(it != table_.end()) {
            // Key already exists -- only the value's byte contribution changes.
            approximate_size_ -= it->second.value.size();
            it->second = Entry{value, false, seq_num};
            approximate_size_ += value.size();
        } else {
            // New key -- charge for both key and value bytes.
            approximate_size_ += key.size() + value.size();
            table_[key] = Entry{value, false, seq_num};
        }
    }

    void Delete(const std::string& key, uint64_t seq_num) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(key);
        if (it != table_.end()) {
            // Key exists (with a value or an earlier tombstone) -- replace it,
            // adjusting only for the value size difference (tombstone value is empty).
            approximate_size_ -= it->second.value.size();
            it->second = Entry{"", true, seq_num};
        } else {
            // Tombstone for a key not yet in the memtable -- still costs key bytes,
            // since it must be stored and eventually flushed to an SSTable.
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
        return result; // std::map iteration is already sorted by key
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

#endif // STD_MAP_MEMTABLE_HPP