// 声明 WAL writer，把 writeBatch 编码追加一个或多个 physical record
#pragma once

#include <cstdint>
#include <cstddef>

#include <lindb/slice.h>
#include <lindb/status.h>

#include "Lin-DB/db/log_format.h"

namespace lindb {

class WritableFile;

namespace log {

class Writer {
public:
    explicit Writer(WritableFile* dest);
    Writer(WritableFile* dest, uint64_t dest_length);
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    Status AddRecord(const Slice& slice);

private:
    Status EmitPhysicalRecord(RecordType type, const char* ptr, size_t length);

    WritableFile* dest_;
    int block_offset_;
};

}

}

// WAL 文件格式：
// +----------+
// | CRC(4B)  |
// +----------+
// | Len(2B)  |
// +----------+
// | Type(1B) |
// +----------+
// | Payload  |
// +----------+