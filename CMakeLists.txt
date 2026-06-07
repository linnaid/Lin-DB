cmake_minimum_required(VERSION 3.16)

project(lindb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lindb_core
    util/arena.cc
    util/coding.cc
    util/comparator.cc
    util/random.cc
    util/slice.cc
    util/status.cc
    util/crc32c.cc
    util/hash.cc
    util/logging.cc
    util/env.cc
    util/bloom.cc 
    util/cache.cc 
    db/db_impl.cc
    db/write_batch.cc
    db/dbformat.cc
    db/memtable.cc
    db/iterator.cc
    db/filename.cc
    db/block_builder.cc 
    db/block.cc
    db/format.cc 
    db/table_builder.cc 
    db/table.cc 
    db/two_level_iterator.cc
    db/filter_block.cc  
    db/table_cache.cc 
    db/log_writer.cc 
    db/log_reader.cc 
    db/version_edit.cc 
    db/version_set.cc 
)

target_include_directories(lindb_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_testing()

add_subdirectory(tests)
