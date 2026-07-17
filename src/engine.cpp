#include "engine.hpp"
#include <filesystem>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

LSMEngine::LSMEngine(std::string data_dir, size_t flush_threshold_bytes)
    : data_dir_(std::move(data_dir)),
      flush_threshold_bytes_(flush_threshold_bytes),
      compaction_manager_(data_dir_) {

    fs::create_directories(data_dir_);
    levels_.resize(2); // L0, L1 for this simplified engine

    memtable_ = std::make_unique<SkipListMemTable>();
    wal_ = std::make_unique<WriteAheadLog>(WalPath());

    LoadExistingSSTables(); // pick up any tables written before a restart
    RecoverFromWAL();       // then replay whatever wasn't flushed yet
}

void LSMEngine::LoadExistingSSTables() {
    if (!fs::exists(data_dir_)) return;

    std::regex pattern(R"(L(\d+)_(\d+)\.sst)");
    std::vector<std::vector<std::pair<uint64_t, std::string>>> found(levels_.size());
    uint64_t max_file_id = 0;

    for (const auto& entry : fs::directory_iterator(data_dir_)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        std::smatch m;
        if (std::regex_match(filename, m, pattern)) {
            uint64_t level = std::stoull(m[1]);
            uint64_t file_id = std::stoull(m[2]);
            if (level >= found.size()) found.resize(level + 1);
            found[level].emplace_back(file_id, entry.path().string());
            max_file_id = std::max(max_file_id, file_id);
        }
    }

    if (found.size() > levels_.size()) levels_.resize(found.size());

    for (size_t level = 0; level < found.size(); ++level) {
        std::sort(found[level].begin(), found[level].end());
        for (const auto& [file_id, path] : found[level]) {
            levels_[level].push_back(SSTable::Open(path));
        }
    }

    next_file_id_ = max_file_id + 1;
}

void LSMEngine::RecoverFromWAL() {
    wal_->Replay(memtable_.get());
}

void LSMEngine::Put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    uint64_t seq = next_seq_num_++;
    wal_->AppendPut(key, value, seq);
    memtable_->Put(key, value, seq);
    MaybeFlush();
}

void LSMEngine::Delete(const std::string& key) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    uint64_t seq = next_seq_num_++;
    wal_->AppendDelete(key, seq);
    memtable_->Delete(key, seq);
    MaybeFlush();
}

std::optional<std::string> LSMEngine::Get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);

    LookupResult r = memtable_->Lookup(key);
    if (r.found) {
        return r.is_tombstone ? std::nullopt : std::optional<std::string>(r.value);
    }

    for (size_t level = 0; level < levels_.size(); ++level) {
        const auto& tables = levels_[level];
        for (auto it = tables.rbegin(); it != tables.rend(); ++it) {
            LookupResult table_r = (*it)->Lookup(key);
            if (table_r.found) {
                return table_r.is_tombstone ? std::nullopt
                                             : std::optional<std::string>(table_r.value);
            }
        }
    }
    return std::nullopt;
}

void LSMEngine::MaybeFlush() {
    if (memtable_->ApproximateSizeBytes() >= flush_threshold_bytes_) {
        FlushMemTableToDisk();
    }
}

void LSMEngine::FlushMemTableToDisk() {
    auto entries = memtable_->GetSortedEntries();
    if (!entries.empty()) {
        std::string path = data_dir_ + "/L0_" + std::to_string(next_file_id_++) + ".sst";
        levels_[0].push_back(SSTable::CreateFromEntries(entries, path, 0));
    }

    memtable_ = std::make_unique<StdMapMemTable>();
    wal_->Reset();

    MaybeCompact();
}

void LSMEngine::MaybeCompact() {
    if (compaction_manager_.NeedsCompaction(levels_[0])) {
        compaction_manager_.Compact(levels_[0], levels_[1], /*target_level_num=*/1,
                                     next_file_id_);
    }
}

size_t LSMEngine::MemTableCount() const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    return memtable_->Count();
}

size_t LSMEngine::SSTableCountAtLevel(size_t level) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (level >= levels_.size()) return 0;
    return levels_[level].size();
}