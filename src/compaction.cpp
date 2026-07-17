#include "compaction.hpp"
#include <queue>
#include <tuple>

std::vector<MemTableEntry> CompactionManager::KWayMerge(
    const std::vector<std::vector<MemTableEntry>>& sorted_lists) const {

    struct HeapItem {
        std::string key;
        uint64_t seq_num;
        size_t list_idx;
        size_t elem_idx;
    };
    struct Cmp {
        bool operator()(const HeapItem& a, const HeapItem& b) const {
            if (a.key != b.key) return a.key > b.key;
            return a.seq_num < b.seq_num;
        }
    };
    std::priority_queue<HeapItem, std::vector<HeapItem>, Cmp> heap;

    for (size_t i = 0; i < sorted_lists.size(); ++i) {
        if (!sorted_lists[i].empty()) {
            heap.push({sorted_lists[i][0].key, sorted_lists[i][0].seq_num, i, 0});
        }
    }

    std::vector<MemTableEntry> result;
    std::string last_key_emitted;
    bool have_last = false;

    while (!heap.empty()) {
        HeapItem top = heap.top();
        heap.pop();

        const MemTableEntry& entry = sorted_lists[top.list_idx][top.elem_idx];

        if (!have_last || entry.key != last_key_emitted) {
            result.push_back(entry);
            last_key_emitted = entry.key;
            have_last = true;
        }

        size_t next_idx = top.elem_idx + 1;
        if (next_idx < sorted_lists[top.list_idx].size()) {
            heap.push({sorted_lists[top.list_idx][next_idx].key,
                       sorted_lists[top.list_idx][next_idx].seq_num,
                       top.list_idx, next_idx});
        }
    }

    return result;
}

void CompactionManager::Compact(std::vector<std::unique_ptr<SSTable>>& source_level,
                                 std::vector<std::unique_ptr<SSTable>>& target_level,
                                 uint64_t target_level_num,
                                 uint64_t& file_id_counter) {
    if (source_level.empty()) return;

    std::vector<std::vector<MemTableEntry>> lists;
    for (auto& table : source_level) {
        lists.push_back(table->GetAllEntries());
    }
    for (auto& table : target_level) {
        lists.push_back(table->GetAllEntries());
    }

    std::vector<MemTableEntry> merged = KWayMerge(lists);

    // Simplification: tombstones are always kept, even though this compaction
    // never runs on the true bottommost level -- a production engine would
    // drop tombstones once compaction reaches the last level. Also, old
    // target_level tables are kept (not removed), so reads stay correct
    // (newest table checked first) but the level grows unboundedly over time.

    if (!merged.empty()) {
        std::string path = data_dir_ + "/L" + std::to_string(target_level_num) +
                            "_" + std::to_string(file_id_counter++) + ".sst";
        target_level.push_back(SSTable::CreateFromEntries(merged, path, target_level_num));
    }

    source_level.clear();
}