#include "engine.hpp"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr size_t kFlushThreshold = 64 * 1024; // 64 KiB
static constexpr size_t kValueSize = 100;
static const std::string kValuePayload(kValueSize, 'x');

static std::string MakeScratchDir(const std::string& tag) {
    return (fs::temp_directory_path() / ("lsm_bench_" + tag)).string();
}

static std::string SeqKey(int64_t i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "key%016lld", (long long)i);
    return buf;
}

static std::vector<int64_t> ShuffledRange(int64_t n, uint64_t seed = 42) {
    std::vector<int64_t> v(n);
    std::iota(v.begin(), v.end(), 0);
    std::mt19937_64 rng(seed);
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

static void RemoveDir(const std::string& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// BM_FillSeq: write N keys in sequential order into a fresh engine
// ---------------------------------------------------------------------------

static void BM_FillSeq(benchmark::State& state) {
    const int64_t n = state.range(0);
    const std::string dir = MakeScratchDir("fillseq");

    for (auto _ : state) {
        state.PauseTiming();
        RemoveDir(dir);
        state.ResumeTiming();

        {
            LSMEngine engine(dir, kFlushThreshold);
            for (int64_t i = 0; i < n; ++i) {
                engine.Put(SeqKey(i), kValuePayload);
            }
        }

        state.PauseTiming();
        RemoveDir(dir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_FillSeq)->Arg(1000)->Arg(10000)->Arg(100000)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// BM_FillRandom: write N keys in random order into a fresh engine
// ---------------------------------------------------------------------------

static void BM_FillRandom(benchmark::State& state) {
    const int64_t n = state.range(0);
    const std::string dir = MakeScratchDir("fillrandom");
    const auto order = ShuffledRange(n);

    for (auto _ : state) {
        state.PauseTiming();
        RemoveDir(dir);
        state.ResumeTiming();

        {
            LSMEngine engine(dir, kFlushThreshold);
            for (int64_t i : order) {
                engine.Put(SeqKey(i), kValuePayload);
            }
        }

        state.PauseTiming();
        RemoveDir(dir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_FillRandom)->Arg(1000)->Arg(10000)->Arg(100000)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Read fixtures: populate N keys once in SetUp, time only the reads
// ---------------------------------------------------------------------------

class ReadFixture : public benchmark::Fixture {
public:
    std::string dir;
    std::unique_ptr<LSMEngine> engine;
    int64_t n = 0;

    void SetUp(const benchmark::State& state) override {
        n = state.range(0);
        dir = MakeScratchDir("read_fixture");
        RemoveDir(dir);
        engine = std::make_unique<LSMEngine>(dir, kFlushThreshold);
        for (int64_t i = 0; i < n; ++i) {
            engine->Put(SeqKey(i), kValuePayload);
        }
    }

    void TearDown(const benchmark::State&) override {
        engine.reset();
        RemoveDir(dir);
    }
};

// ---------------------------------------------------------------------------
// BM_ReadSeq: sequential reads against a pre-populated engine
// ---------------------------------------------------------------------------

BENCHMARK_DEFINE_F(ReadFixture, BM_ReadSeq)(benchmark::State& state) {
    int64_t key_idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine->Get(SeqKey(key_idx % n)));
        ++key_idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(ReadFixture, BM_ReadSeq)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// BM_ReadRandom: random reads against a pre-populated engine
// ---------------------------------------------------------------------------

BENCHMARK_DEFINE_F(ReadFixture, BM_ReadRandom)(benchmark::State& state) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int64_t> dist(0, n - 1);
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine->Get(SeqKey(dist(rng))));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(ReadFixture, BM_ReadRandom)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// BM_MixedReadWrite: 90% reads / 10% writes against a pre-populated engine
// ---------------------------------------------------------------------------

BENCHMARK_DEFINE_F(ReadFixture, BM_MixedReadWrite)(benchmark::State& state) {
    std::mt19937_64 rng(99);
    std::uniform_int_distribution<int64_t> key_dist(0, n - 1);
    std::uniform_int_distribution<int> op_dist(0, 9); // 0 = write, 1-9 = read

    for (auto _ : state) {
        int64_t k = key_dist(rng);
        if (op_dist(rng) == 0) {
            engine->Put(SeqKey(k), kValuePayload);
        } else {
            benchmark::DoNotOptimize(engine->Get(SeqKey(k)));
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(ReadFixture, BM_MixedReadWrite)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// BM_Recovery: time constructing a new LSMEngine over existing data on disk
// (WAL replay + SSTable loading)
// ---------------------------------------------------------------------------

class RecoveryFixture : public benchmark::Fixture {
public:
    std::string dir;
    int64_t n = 0;

    void SetUp(const benchmark::State& state) override {
        n = state.range(0);
        dir = MakeScratchDir("recovery");
        RemoveDir(dir);
        {
            LSMEngine engine(dir, kFlushThreshold);
            for (int64_t i = 0; i < n; ++i) {
                engine.Put(SeqKey(i), kValuePayload);
            }
        }
    }

    void TearDown(const benchmark::State&) override {
        RemoveDir(dir);
    }
};

BENCHMARK_DEFINE_F(RecoveryFixture, BM_Recovery)(benchmark::State& state) {
    for (auto _ : state) {
        {
            LSMEngine engine(dir, kFlushThreshold);
            benchmark::DoNotOptimize(engine.MemTableCount());
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(RecoveryFixture, BM_Recovery)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
