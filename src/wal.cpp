#include "wal.hpp"
#include <stdexcept>
#include <cstring>

WriteAheadLog::WriteAheadLog(const std::string& path) : path_(path) {
    OpenForAppend();
}

WriteAheadLog::~WriteAheadLog() {
    if (file_.is_open()) file_.close();
}

void WriteAheadLog::OpenForAppend() {
    std::fstream create(path_, std::ios::app | std::ios::binary);
    create.close();
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::ate);
    if (!file_.is_open()) {
        throw std::runtime_error("WriteAheadLog: failed to open " + path_);
    }
}

void WriteAheadLog::WriteRecord(RecordType type, const std::string& key,
                                const std::string& value, uint64_t seq_num) {
    uint8_t t = static_cast<uint8_t>(type);
    uint32_t key_len = static_cast<uint32_t>(key.size());
    uint32_t value_len = static_cast<uint32_t>(value.size());

    file_.write(reinterpret_cast<const char*>(&t), sizeof(t));
    file_.write(reinterpret_cast<const char*>(&seq_num), sizeof(seq_num));
    file_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    file_.write(key.data(), key_len);
    file_.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
    if (value_len > 0) file_.write(value.data(), value_len);

    file_.flush(); // flush() reaches the OS buffer, not necessarily disk --
                   // a real system would call fsync() here based on a
                   // configurable durability policy (documented simplification).
}

void WriteAheadLog::AppendPut(const std::string& key, const std::string& value, uint64_t seq_num) {
    WriteRecord(kPut, key, value, seq_num);
}

void WriteAheadLog::AppendDelete(const std::string& key, uint64_t seq_num) {
    WriteRecord(kDelete, key, "", seq_num);
}

void WriteAheadLog::Replay(MemTable* memtable) {
    file_.clear();
    file_.seekg(0, std::ios::beg);

    while (true) {
        uint8_t type;
        uint64_t seq_num;
        uint32_t key_len;

        if (!file_.read(reinterpret_cast<char*>(&type), sizeof(type))) break;
        if (!file_.read(reinterpret_cast<char*>(&seq_num), sizeof(seq_num))) break;
        if (!file_.read(reinterpret_cast<char*>(&key_len), sizeof(key_len))) break;

        std::string key(key_len, '\0');
        if (!file_.read(key.data(), key_len)) break;

        uint32_t value_len;
        if (!file_.read(reinterpret_cast<char*>(&value_len), sizeof(value_len))) break;

        std::string value;
        if (value_len > 0) {
            value.resize(value_len);
            if (!file_.read(value.data(), value_len)) break;
        }

        if (type == kPut) {
            memtable->Put(key, value, seq_num);
        } else {
            memtable->Delete(key, seq_num);
        }
    }

    file_.clear();
    file_.seekp(0, std::ios::end);
}

void WriteAheadLog::Reset() {
    file_.close();
    file_.open(path_, std::ios::out | std::ios::binary | std::ios::trunc);
    file_.close();
    OpenForAppend();
}