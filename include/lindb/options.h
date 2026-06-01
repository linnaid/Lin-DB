// 定义 DB 打开、读取、写入时用到的配置对象
#pragma once

#include <cstddef>

#include <lindb/comparator.h>
#include <lindb/env.h>

namespace lindb {
class Cache;
class FilterPolicy;
class Snapshot;

enum CompressionType {
    kNoCompression = 0x0,
    kSnappyCompression = 0x1,
    kZstdCompression = 0x2,
};
    
// 定义打开 DB 时使用的配置集合
struct Options {
    const Comparator* comparator = BytewiseComparator();
    // 后续 Open/Recover 决定是否自动建库
    bool create_if_missing = false;
    // 后续 Open 决定是否拒绝打开已有库
    bool error_if_exists = false;
    // 后续读校验/恢复路径用
    bool paranoid_checks = false;
    Env* env = Env::Default();
    Logger* info_log = nullptr;
    size_t write_buffer_size = 4 * 1024 * 1024;
    int max_open_files = 1000;
    Cache* block_cache = nullptr;
    size_t block_size = 4 * 1024;
    int block_restart_interval = 16;
    size_t max_file_size = 2 * 1024 * 1024;
    CompressionType compression = kNoCompression;
    bool reuse_logs = false;
    const FilterPolicy* filter_policy = nullptr;
};

// 定义读操作配置。
struct ReadOptions {
    // 读取 block 时是否校验 checksum(crc)
    bool verify_checksums = false;
    // 读取的数据是否放入 block cache
    bool fill_cache = true;
    const Snapshot* snapshot = nullptr;
};

// 定义写操作配置。
struct WriteOptions {
    bool sync = false;
};

}  // namespace lindb
