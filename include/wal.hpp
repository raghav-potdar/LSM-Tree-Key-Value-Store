#pragma once
#include "memtable.hpp"
#include <string>
#include <fstream>

// Append-only log of every write, used to rebuild the memtable after a crash.
// Record format on disk:
//   uint8_t  type        (0 = Put, 1 = Delete)
//   uint64_t seq_num
//   uint32_t key_len
//   bytes    key
//   uint32_t value_len    (0 for Delete)
//   bytes    value        (absent for Delete)
class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& path);
    ~WriteAheadLog();

    void AppendPut(const std::string& key, const std::string& value, uint64_t seq_num);
    void AppendDelete(const std::string& key, uint64_t seq_num);

    void Replay(MemTable* memtable);
    void Reset();

private:
    enum RecordType : uint8_t { kPut = 0, kDelete = 1 };
    std::string path_;
    std::fstream file_;

    void WriteRecord(RecordType type, const std::string& key,
                      const std::string& value, uint64_t seq_num);
    void OpenForAppend();
};
