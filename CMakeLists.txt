cmake_minimum_required(VERSION 3.10)
project(lsm_kv CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(lsm_kv
    src/main.cpp
)

target_link_libraries(lsm_kv pthread)