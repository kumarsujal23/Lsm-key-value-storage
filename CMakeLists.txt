cmake_minimum_required(VERSION 3.16)
project(LSMStorageEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release build for performance, Debug for development
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -fsanitize=address,undefined")

# All source files
set(SOURCES
    bloom_filter.cpp
    memtable.cpp
    sstable.cpp
    merge_utils.cpp
    wal.cpp
    lsm.cpp
)

# Main executable
add_executable(lsm_engine ${SOURCES} main.cpp)

target_include_directories(lsm_engine PRIVATE ${CMAKE_SOURCE_DIR})
