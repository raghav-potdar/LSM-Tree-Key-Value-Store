#include "sstable.hpp"
#include <algorithm>
#include <stdexcept>
#include <cstring>

void SSTable::WriteRecord(std::ofstream& out, const MemTableEntry& e) {
    uint8_t tomb = e.is_tombstone ? 1 : 0;
    uint64_t seq = e.seq_num;
    uint32_t key_len = static_cast<uint32_t>(e.key.size());
    uint32_t value_len = e.is_tombstone ? 0 : static_cast<uint32_t>(e.value.size());

    out.write(reinterpret_cast<const char*>(&tomb), sizeof(tomb));
    out.write(reinterpret_cast<const char*>(&seq), sizeof(seq));
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    out.write(e.key.data(), key_len);
    out.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
    if (value_len > 0) out.write(e.value.data(), value_len);
}

bool SSTable::ReadRecordAt(std::ifstream& in, uint64_t offset, Record* out) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);

    uint8_t tomb;
    uint64_t seq;
    uint32_t key_len;

    if (!in.read(reinterpret_cast<char*>(&tomb), sizeof(tomb))) return false;
    if (!in.read(reinterpret_cast<char*>(&seq), sizeof(seq))) return false;
    if (!in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len))) return false;

    std::string key(key_len, '\0');
    if (!in.read(key.data(), key_len)) return false;

    uint32_t value_len;
    if (!in.read(reinterpret_cast<char*>(&value_len), sizeof(value_len))) return false;

    std::string value;
    if (value_len > 0) {
        value.resize(value_len);
        if (!in.read(value.data(), value_len)) return false;
    }

    out->is_tombstone = (tomb != 0);
    out->seq_num = seq;
    out->key = std::move(key);
    out->value = std::move(value);
    return true;
}

std::unique_ptr<SSTable> SSTable::CreateFromEntries(
    const std::vector<MemTableEntry>& entries,
    const std::string& file_path,
    uint64_t level) {

    auto table = std::make_unique<SSTable>();
    table->file_path_ = file_path;
    table->level_ = level;

    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("SSTable::CreateFromEntries: cannot open " + file_path);
    }

    BloomFilter bf(std::max<size_t>(entries.size(), 1), 0.01);

    uint64_t offset = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        bf.Add(e.key);

        if (i == 0) table->min_key_ = e.key;
        if (i + 1 == entries.size()) table->max_key_ = e.key;

        if (i % kIndexInterval == 0) {
            table->sparse_index_.emplace_back(e.key, offset);
        }

        WriteRecord(out, e);

        offset += sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t) +
                  e.key.size() + sizeof(uint32_t) +
                  (e.is_tombstone ? 0 : e.value.size());
    }

    uint64_t data_end = offset;
    table->index_offset_ = data_end;

    uint32_t index_count = static_cast<uint32_t>(table->sparse_index_.size());
    out.write(reinterpret_cast<const char*>(&index_count), sizeof(index_count));
    for (const auto& [key, off] : table->sparse_index_) {
        uint32_t key_len = static_cast<uint32_t>(key.size());
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(key.data(), key_len);
        out.write(reinterpret_cast<const char*>(&off), sizeof(off));
    }
    uint64_t index_size = 4;
    for (const auto& [key, off] : table->sparse_index_) {
        index_size += 4 + key.size() + 8;
    }

    uint64_t bloom_offset = data_end + index_size;
    std::vector<uint8_t> bloom_bytes = bf.Serialize();
    out.write(reinterpret_cast<const char*>(bloom_bytes.data()), bloom_bytes.size());
    uint64_t bloom_size = bloom_bytes.size();

    out.write(reinterpret_cast<const char*>(&data_end), sizeof(data_end));
    out.write(reinterpret_cast<const char*>(&index_size), sizeof(index_size));
    out.write(reinterpret_cast<const char*>(&bloom_offset), sizeof(bloom_offset));
    out.write(reinterpret_cast<const char*>(&bloom_size), sizeof(bloom_size));
    uint64_t magic = kMagic;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

    out.close();

    table->bloom_filter_ = std::move(bf);
    return table;
}

std::unique_ptr<SSTable> SSTable::Open(const std::string& file_path) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("SSTable::Open: cannot open " + file_path);
    }

    in.seekg(-static_cast<std::streamoff>(40), std::ios::end);
    uint64_t index_offset, index_size, bloom_offset, bloom_size, magic;
    in.read(reinterpret_cast<char*>(&index_offset), sizeof(index_offset));
    in.read(reinterpret_cast<char*>(&index_size), sizeof(index_size));
    in.read(reinterpret_cast<char*>(&bloom_offset), sizeof(bloom_offset));
    in.read(reinterpret_cast<char*>(&bloom_size), sizeof(bloom_size));
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));

    if (magic != kMagic) {
        throw std::runtime_error("SSTable::Open: bad magic in " + file_path);
    }

    auto table = std::make_unique<SSTable>();
    table->file_path_ = file_path;
    table->index_offset_ = index_offset;

    in.clear();
    in.seekg(static_cast<std::streamoff>(index_offset), std::ios::beg);
    uint32_t index_count;
    in.read(reinterpret_cast<char*>(&index_count), sizeof(index_count));
    table->sparse_index_.reserve(index_count);
    for (uint32_t i = 0; i < index_count; ++i) {
        uint32_t key_len;
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        std::string key(key_len, '\0');
        in.read(key.data(), key_len);
        uint64_t off;
        in.read(reinterpret_cast<char*>(&off), sizeof(off));
        table->sparse_index_.emplace_back(key, off);
    }
    if (!table->sparse_index_.empty()) {
        table->min_key_ = table->sparse_index_.front().first;
    }

    in.clear();
    in.seekg(static_cast<std::streamoff>(bloom_offset), std::ios::beg);
    std::vector<uint8_t> bloom_bytes(bloom_size);
    in.read(reinterpret_cast<char*>(bloom_bytes.data()), bloom_size);
    table->bloom_filter_ = BloomFilter::Deserialize(bloom_bytes);

    if (!table->sparse_index_.empty()) {
        uint64_t last_offset = table->sparse_index_.back().second;
        Record rec;
        std::string last_key = table->sparse_index_.back().first;
        uint64_t cur = last_offset;
        while (cur < index_offset) {
            if (!ReadRecordAt(in, cur, &rec)) break;
            last_key = rec.key;
            cur += sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t) +
                   rec.key.size() + sizeof(uint32_t) +
                   (rec.is_tombstone ? 0 : rec.value.size());
        }
        table->max_key_ = last_key;
    }

    return table;
}

LookupResult SSTable::Lookup(const std::string& key) const {
    if (!bloom_filter_.MightContain(key)) return LookupResult{false, false, ""};
    if (sparse_index_.empty()) return LookupResult{false, false, ""};

    auto it = std::upper_bound(
        sparse_index_.begin(), sparse_index_.end(), key,
        [](const std::string& k, const std::pair<std::string, uint64_t>& entry) {
            return k < entry.first;
        });
    if (it == sparse_index_.begin()) return LookupResult{false, false, ""};
    --it;
    uint64_t start_offset = it->second;

    auto next_it = it + 1;
    uint64_t end_offset = (next_it != sparse_index_.end()) ? next_it->second : index_offset_;

    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) return LookupResult{false, false, ""};

    uint64_t offset = start_offset;
    while (offset < end_offset) {
        Record rec;
        if (!ReadRecordAt(in, offset, &rec)) break;
        if (rec.key == key) {
            if (rec.is_tombstone) return LookupResult{true, true, ""};
            return LookupResult{true, false, rec.value};
        }
        if (rec.key > key) break;
        offset += sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t) +
                  rec.key.size() + sizeof(uint32_t) +
                  (rec.is_tombstone ? 0 : rec.value.size());
    }
    return LookupResult{false, false, ""};
}

std::optional<std::string> SSTable::Get(const std::string& key) const {
    LookupResult r = Lookup(key);
    if (!r.found || r.is_tombstone) return std::nullopt;
    return r.value;
}

std::vector<MemTableEntry> SSTable::GetAllEntries() const {
    std::vector<MemTableEntry> result;
    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) return result;

    uint64_t offset = 0;
    while (offset < index_offset_) {
        Record rec;
        if (!ReadRecordAt(in, offset, &rec)) break;
        result.push_back({rec.key, rec.value, rec.is_tombstone, rec.seq_num});
        offset += sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t) +
                  rec.key.size() + sizeof(uint32_t) +
                  (rec.is_tombstone ? 0 : rec.value.size());
    }
    return result;
}

bool SSTable::OverlapsRange(const std::string& key_lo, const std::string& key_hi) const {
    if (min_key_.empty() && max_key_.empty()) return false;
    return !(max_key_ < key_lo || min_key_ > key_hi);
}