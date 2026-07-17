#pragma once
#include <string>
#include <optional>
#include <vector>
#include <cstdint>

// A single entry as it will eventually be written to an SSTable.
struct MemTableEntry {
    std::string key;
    std::string value;
    bool is_tombstone;
    uint64_t seq_num;   // orders writes to the same key across flushes/SSTables
};

// Distinguishes "key absent from this store" from "key present here as a
// tombstone" -- collapsing these two (as a plain optional<string> does) is
// incorrect once there's older data (an older SSTable, or an older level)
// that a tombstone needs to shadow: treating "tombstoned" the same as
// "not found" lets the search fall through and resurrect the stale value.
struct LookupResult {
    bool found = false;        // true if the key exists in this store at all
    bool is_tombstone = false; // only meaningful if found == true
    std::string value;         // only meaningful if found && !is_tombstone
};

class MemTable {
public:
    virtual ~MemTable() = default;

    virtual void Put(const std::string& key, const std::string& value, uint64_t seq_num) = 0;
    virtual void Delete(const std::string& key, uint64_t seq_num) = 0;

    // Returns value if present and not a tombstone. Empty optional otherwise.
    // Kept for convenience/simple callers; internal engine logic should use
    // Lookup() instead so it can correctly shadow older data on a tombstone.
    virtual std::optional<std::string> Get(const std::string& key) const = 0;

    // Tri-state lookup: distinguishes not-found / tombstoned / found-with-value.
    virtual LookupResult Lookup(const std::string& key) const = 0;

    virtual size_t ApproximateSizeBytes() const = 0;

    // Sorted, full iteration (ascending key order), includes tombstones.
    virtual std::vector<MemTableEntry> GetSortedEntries() const = 0;

    virtual size_t Count() const = 0;
};