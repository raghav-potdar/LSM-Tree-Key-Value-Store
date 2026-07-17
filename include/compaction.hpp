#pragma once
#include "sstable.hpp"
#include <vector>
#include <memory>
#include <string>

// Handles merging SSTables at one level down into the next level.
// Uses size-tiered triggering: once a level has more than kTriggerCount
// tables, all of them are merged into new table(s) one level down.
class CompactionManager {
public:
    static constexpr size_t kTriggerCount = 4;

    explicit CompactionManager(std::string data_dir) : data_dir_(std::move(data_dir)) {}

    bool NeedsCompaction(const std::vector<std::unique_ptr<SSTable>>& level) const {
        return level.size() > kTriggerCount;
    }

    void Compact(std::vector<std::unique_ptr<SSTable>>& source_level,
                 std::vector<std::unique_ptr<SSTable>>& target_level,
                 uint64_t target_level_num,
                 uint64_t& file_id_counter);

private:
    std::string data_dir_;

    std::vector<MemTableEntry> KWayMerge(
        const std::vector<std::vector<MemTableEntry>>& sorted_lists) const;
};