// 声明 TableCache，对外提供“按 file number 查找/open SSTable”的统一入口
// 为后续 Version/DBImpl 读路径复用打开过的 table
#pragma once

#include <cstdint>
#include <string>

#include <lindb/cache.h>
#include <lindb/options.h>
#include<lindb/status.h>
#include <lindb/table.h>

namespace lindb {

class Iterator;
struct ReadOptions;
class Slice;

class TableCache {
public:
    TableCache(const std::string& dbname, const Options& options, int entries);
    TableCache(const TableCache&) = delete;
    TableCache& operator=(const TableCache&) = delete;

    ~TableCache();

    Iterator* NewIterator(const ReadOptions& options, uint64_t file_number, uint64_t file_size);
    Status Get(const ReadOptions& options, uint64_t file_number, uint64_t file_size, const Slice& key, 
               void* arg, void (*handle_result)(void*, const Slice&, const Slice&));
    // 从缓存中移除指定 table，compaction 删除文件前调用
    void Evict(uint64_t file_number);

private:
    Status FindTable(uint64_t file_number, uint64_t file_size, Cache::Handle** handle);

    std::string dbname_;
    Options options_;
    Cache* cache_;
};

}