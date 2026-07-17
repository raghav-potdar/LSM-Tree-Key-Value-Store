#pragma once
#include "memtable.hpp"
#include "std_map_memtable.hpp"
#include "skip_list_memtable.hpp"
#include "wal.hpp"
#include "sstable.hpp"
#include "compaction.hpp"
#include <memory>
#include <vector>
#include <string>
#include <mutex>

class LSMEngine {
public:
    explicit LSMEngine(std::string data_dir, size_t flush_threshold_bytes = 4 * 1024 * 1024);

    void Put(const std::string& key, const std::string& value);
    void Delete(const std::string& key);
    std::optional<std::string> Get(const std::string& key) const;

    size_t MemTableCount() const;
    size_t SSTableCountAtLevel(size_t level) const;

private:
    std::string data_dir_;
    size_t flush_threshold_bytes_;

    std::unique_ptr<MemTable> memtable_;
    std::unique_ptr<WriteAheadLog> wal_;
    std::vector<std::vector<std::unique_ptr<SSTable>>> levels_;
    CompactionManager compaction_manager_;

    uint64_t next_seq_num_ = 0;
    uint64_t next_file_id_ = 0;

    mutable std::mutex engine_mutex_;

    void MaybeFlush();
    void FlushMemTableToDisk();
    void MaybeCompact();
    void RecoverFromWAL();
    void LoadExistingSSTables();
    std::string WalPath() const { return data_dir_ + "/wal.log"; }
};