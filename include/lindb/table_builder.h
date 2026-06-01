// 定义“把有序 key/value iterator 输出成 .ldb 文件”的公共接口
#pragma once

#include <cstdint>

#include <lindb/options.h>
#include <lindb/status.h>

namespace lindb {

class Slice;
class WritableFile;
class BlockBuilder;
class BlockHandle;

class TableBuilder {
public:
    TableBuilder(const Options& options, WritableFile* file);
    TableBuilder(const TableBuilder&) = delete;
    TableBuilder& operator=(const TableBuilder&) = delete;
    ~TableBuilder();

    Status ChangeOptions(const Options& options);
    void Add(const Slice& key, const Slice& value);
    void Flush();
    Status status() const;
    // 在这里将 footer 写入SSTable
    Status Finish();
    void Abandon();
    uint64_t NumEntries() const;
    uint64_t FileSize() const;

private:
    bool ok() const { return status().ok(); }
    void WriteBlock(BlockBuilder* block, BlockHandle* handle);
    // 在这里创建 BlockHandle
    void WriteRawBlock(const Slice& data, CompressionType type, BlockHandle* handle);
    struct Rep;
    Rep* rep_;
};

}