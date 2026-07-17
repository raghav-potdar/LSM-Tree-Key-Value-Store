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

## Benchmarks

Benchmarks use [Google Benchmark](https://github.com/google/benchmark) (fetched automatically via CMake) and cover sequential/random fills, sequential/random reads, mixed read-write workloads, and WAL/SSTable recovery.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target lsm_bench -j$(nproc)
./build/lsm_bench
```

### `std::map` vs skip list memtable

The engine's in-memory memtable can be backed by either `std::map` or a skip list. Results below are from two full benchmark runs, one with each backend:

| Benchmark | std::map | skip list |
|---|---|---|
| FillSeq/1000 | 4.61 ms (216.8k/s) | 4.77 ms (209.7k/s) |
| FillSeq/10000 | 88.7 ms (112.8k/s) | 89.3 ms (112.0k/s) |
| FillSeq/100000 | 36781 ms (2.72k/s) | 36650 ms (2.73k/s) |
| FillRandom/1000 | 4.94 ms (202.4k/s) | 5.07 ms (197.7k/s) |
| FillRandom/10000 | 89.3 ms (112.0k/s) | 92.5 ms (108.2k/s) |
| FillRandom/100000 | 37583 ms (2.66k/s) | 38305 ms (2.61k/s) |
| ReadSeq/1000 | 8.79 us (113.8k/s) | 8.34 us (119.9k/s) |
| ReadSeq/10000 | 15.3 us (65.2k/s) | 15.5 us (64.6k/s) |
| ReadSeq/100000 | 15.3 us (65.3k/s) | 15.0 us (66.7k/s) |
| ReadRandom/1000 | 8.66 us (115.4k/s) | 8.40 us (119.1k/s) |
| ReadRandom/10000 | 15.7 us (63.5k/s) | 15.6 us (63.9k/s) |
| ReadRandom/100000 | 15.8 us (63.2k/s) | 16.1 us (62.1k/s) |
| MixedReadWrite/1000 | 10.3 us (97.3k/s) | 11.4 us (88.2k/s) |
| MixedReadWrite/10000 | 16.5 us (60.7k/s) | 16.8 us (59.6k/s) |
| MixedReadWrite/100000 | 83.2 us (12.0k/s) | 85.2 us (11.7k/s) |
| Recovery/1000 | 0.295 ms (3.39k/s) | 0.309 ms (3.25k/s) |
| Recovery/10000 | 0.613 ms (1.63k/s) | 0.640 ms (1.56k/s) |
| Recovery/100000 | 19.6 ms (51.2/s) | 22.2 ms (45.0/s) |

Measured on a 16-core 2900 MHz CPU. Numbers are close across the board, with `std::map` slightly ahead on writes and recovery, and the skip list slightly ahead on small sequential/random reads.
