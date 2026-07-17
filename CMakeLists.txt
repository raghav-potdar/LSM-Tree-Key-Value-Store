cmake_minimum_required(VERSION 3.10)
project(lsm_kv CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(lsm_kv_core STATIC
    src/engine.cpp
    src/wal.cpp
    src/sstable.cpp
    src/bloom_filter.cpp
    src/compaction.cpp
)
target_include_directories(lsm_kv_core PUBLIC include)
target_link_libraries(lsm_kv_core PUBLIC pthread)

add_executable(lsm_kv src/main.cpp)
target_link_libraries(lsm_kv PRIVATE lsm_kv_core)

option(LSM_KV_BUILD_BENCHMARKS "Build lsm_bench target" ON)
if(LSM_KV_BUILD_BENCHMARKS)
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.5
    )
    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(lsm_bench bench/bench_main.cpp)
    target_link_libraries(lsm_bench PRIVATE lsm_kv_core benchmark::benchmark)
endif()