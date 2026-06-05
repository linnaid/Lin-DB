#include "Lin-DB/db/log_writer.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <lindb/env.h>

#include "Lin-DB/db/log_format.h"
#include "Lin-DB/util/coding.h"
#include "Lin-DB/util/crc32c.h"

namespace lindb {
namespace log {

Writer::Writer(WritableFile* dest)
    : dest_(dest), block_offset_(0) {}

Writer::Writer(WritableFile* dest, uint64_t dest_length)
    : dest_(dest), block_offset_(static_cast<int>(dest_length % kBlockSize)) {}

Status Writer::AddRecord(const Slice& slice) {
    const char* ptr = slice.data();
    size_t left = slice.size();
    bool begin = true;

    Status status;
    do {
        const int leftover = kBlockSize - block_offset_;
        assert(leftover >= 0);
        if (leftover < kHeaderSize) {
            if (leftover > 0) {
                static const char zeros[kHeaderSize] = {0};
                status = dest_->Append(Slice(zeros, leftover));
                if (!status.ok()) {
                    return status;
                }
            }
            block_offset_ = 0;
        }

        const size_t avail = kBlockSize - block_offset_ - kHeaderSize;
        const size_t fragment_length = (left < avail) ? left : avail;
        const bool end = (left == fragment_length);

        RecordType type;
        if (begin && end) {
            type = kFullType;
        } else if (begin) {
            type = kFirstType;
        } else if (end) {
            type = kLastType;
        } else {
            type = kMiddleType;
        }

        status = EmitPhysicalRecord(type, ptr, fragment_length);
        if (!status.ok()) {
            return status;
        }

        ptr += fragment_length;
        left -= fragment_length;
        begin = false;
    } while (left > 0);

    return Status::OK();
}

Status Writer::EmitPhysicalRecord(RecordType type, const char* ptr, size_t length) {
    assert(length <= 0xffff);
    assert(block_offset_ + kHeaderSize + length <= kBlockSize);

    char header[kHeaderSize];
    header[4] = static_cast<char>(length & 0xff);
    header[5] = static_cast<char>(length >> 8);
    header[6] = static_cast<char>(type);

    uint32_t crc = crc32c::Value(header + 6, 1);
    crc = crc32c::Extend(crc, ptr, length);
    EncodeFixed32(header, crc32c::Mask(crc));

    Status status = dest_->Append(Slice(header, kHeaderSize));
    if (status.ok()) {
        status = dest_->Append(Slice(ptr, length));
    }
    if (status.ok()) {
        status = dest_->Flush();
    }
    block_offset_ += static_cast<int>(kHeaderSize + length);
    return status;
}

}
}