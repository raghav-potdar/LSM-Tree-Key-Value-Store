#pragma once
#include "memtable.hpp"
#include <vector>
#include <string>
#include <mutex>
#include <random>
#include <cstdint>

// Classic probabilistic skip list, used as a drop-in replacement for
// StdMapMemTable. Same MemTable interface, so swapping is a one-line change
// wherever a MemTable is constructed (e.g. std::make_unique<SkipListMemTable>()).
//
// Why a skip list over std::map here: RocksDB/LevelDB use skip lists for
// memtables because (a) node inserts don't require rebalancing the way a
// red-black tree does, and (b) it's straightforward to make reads lock-free
// against a single concurrent writer later, which a std::map cannot do
// without a coarse lock around the whole structure.
class SkipListMemTable : public MemTable {
public:
    explicit SkipListMemTable(size_t max_level = 16, double p = 0.5)
        : max_level_(max_level), p_(p), rng_(std::random_device{}()) {
        head_ = new Node("", max_level_);
        current_level_ = 1;
    }

    ~SkipListMemTable() override {
        Node* cur = head_->forward[0];
        while (cur != nullptr) {
            Node* next = cur->forward[0];
            delete cur;
            cur = next;
        }
        delete head_;
    }

    // Non-copyable: raw pointers inside make copy semantics unsafe as written.
    SkipListMemTable(const SkipListMemTable&) = delete;
    SkipListMemTable& operator=(const SkipListMemTable&) = delete;

    void Put(const std::string& key, const std::string& value, uint64_t seq_num) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Node*> update(max_level_, nullptr);
        Node* node = FindPredecessors(key, update);

        if (node != nullptr && node->key == key) {
            approximate_size_ -= node->value.size();
            node->value = value;
            node->is_tombstone = false;
            node->seq_num = seq_num;
            approximate_size_ += value.size();
            return;
        }

        InsertNewNode(key, value, /*is_tombstone=*/false, seq_num, update);
        approximate_size_ += key.size() + value.size();
        ++count_;
    }

    void Delete(const std::string& key, uint64_t seq_num) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Node*> update(max_level_, nullptr);
        Node* node = FindPredecessors(key, update);

        if (node != nullptr && node->key == key) {
            approximate_size_ -= node->value.size();
            node->value.clear();
            node->is_tombstone = true;
            node->seq_num = seq_num;
            return;
        }

        InsertNewNode(key, "", /*is_tombstone=*/true, seq_num, update);
        approximate_size_ += key.size();
        ++count_;
    }

    std::optional<std::string> Get(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const Node* node = FindNode(key);
        if (node == nullptr || node->is_tombstone) return std::nullopt;
        return node->value;
    }

    LookupResult Lookup(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const Node* node = FindNode(key);
        if (node == nullptr) return LookupResult{false, false, ""};
        if (node->is_tombstone) return LookupResult{true, true, ""};
        return LookupResult{true, false, node->value};
    }

    size_t ApproximateSizeBytes() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return approximate_size_;
    }

    std::vector<MemTableEntry> GetSortedEntries() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MemTableEntry> result;
        result.reserve(count_);
        for (Node* cur = head_->forward[0]; cur != nullptr; cur = cur->forward[0]) {
            result.push_back({cur->key, cur->value, cur->is_tombstone, cur->seq_num});
        }
        return result; // level-0 linked list is already sorted by key
    }

    size_t Count() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    struct Node {
        std::string key;
        std::string value;
        bool is_tombstone = false;
        uint64_t seq_num = 0;
        std::vector<Node*> forward;

        Node(const std::string& k, size_t levels)
            : key(k), forward(levels, nullptr) {}
    };

    // Walks from the top level down, filling `update` with the last node at
    // each level whose key is < target key. Returns the exact-match node if
    // the search lands on one at level 0, else nullptr.
    Node* FindPredecessors(const std::string& key, std::vector<Node*>& update) {
        Node* cur = head_;
        for (int level = static_cast<int>(current_level_) - 1; level >= 0; --level) {
            while (cur->forward[level] != nullptr && cur->forward[level]->key < key) {
                cur = cur->forward[level];
            }
            update[level] = cur;
        }
        Node* candidate = cur->forward[0];
        return (candidate != nullptr && candidate->key == key) ? candidate : nullptr;
    }

    const Node* FindNode(const std::string& key) const {
        Node* cur = head_;
        for (int level = static_cast<int>(current_level_) - 1; level >= 0; --level) {
            while (cur->forward[level] != nullptr && cur->forward[level]->key < key) {
                cur = cur->forward[level];
            }
        }
        Node* candidate = cur->forward[0];
        return (candidate != nullptr && candidate->key == key) ? candidate : nullptr;
    }

    void InsertNewNode(const std::string& key, const std::string& value, bool is_tombstone,
                        uint64_t seq_num, std::vector<Node*>& update) {
        size_t new_level = RandomLevel();
        if (new_level > current_level_) {
            for (size_t level = current_level_; level < new_level; ++level) {
                update[level] = head_;
            }
            current_level_ = new_level;
        }

        Node* new_node = new Node(key, new_level);
        new_node->value = value;
        new_node->is_tombstone = is_tombstone;
        new_node->seq_num = seq_num;

        for (size_t level = 0; level < new_level; ++level) {
            new_node->forward[level] = update[level]->forward[level];
            update[level]->forward[level] = new_node;
        }
    }

    // Coin-flip levels: level 1 always, each additional level with probability p.
    size_t RandomLevel() {
        size_t level = 1;
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        while (level < max_level_ && dist(rng_) < p_) {
            ++level;
        }
        return level;
    }

    size_t max_level_;
    double p_;
    std::mt19937 rng_;

    Node* head_;
    size_t current_level_;
    size_t count_ = 0;
    size_t approximate_size_ = 0;

    mutable std::mutex mutex_;
};