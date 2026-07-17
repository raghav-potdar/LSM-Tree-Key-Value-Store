# LSM-Tree Key-Value Store

A simple key-value store engine in C++ built on a Log-Structured Merge (LSM) tree.

## Features

- `Put`, `Delete`, and `Get` operations
- In-memory memtable (std::map or skip list) with configurable flush threshold
- Write-ahead log (WAL) for crash recovery
- SSTables persisted to disk, organized into levels
- Bloom filters for faster negative lookups
- Background compaction across levels

## Project Structure

```
include/    Header files (engine, memtable, wal, sstable, bloom filter, compaction)
src/        Implementation files
```

## Build & Run

```bash
mkdir build && cd build
cmake ..
make
./lsm_kv
```

## Usage

```cpp
LSMEngine engine("./data");

engine.Put("key", "value");
engine.Get("key");     // returns std::optional<std::string>
engine.Delete("key");
```
