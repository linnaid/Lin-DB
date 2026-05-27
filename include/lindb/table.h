// Table 公开接口：Open、NewIterator、InternalGet、ApproximateOffsetOf
// 定义 SSTable 读取接口
#pragma once

#include <cstdint>

#include <lindb/iterator.h>
#include <lindb/slice.h>
#include <lindb/status.h>

namespace lindb {

class RandomAccessFile;
struct Options;
struct ReadOptions;
class Block;

class Table {
public:
    static Status Open(const Options& options, 
        RandomAccessFile* file, uint64_t file_size, Table** table);
    
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    ~Table();

    Iterator* NewIterator(const ReadOptions& options) const;

    Status InternalGet(const ReadOptions& options, const Slice& key, 
        void* arg, void (*handle_result)(void*, const Slice&, const Slice&)) const;

private:
    struct Rep;

    explicit Table(Rep* rep);

    static Iterator* BlockReader(void* arg, const ReadOptions& options, 
        const Slice& index_value);
    
    Rep* const rep_;
};

}