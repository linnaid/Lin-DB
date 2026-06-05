#include "Lin-DB/db/log_reader.h"

#include <cstdint>
#include <string>

#include <lindb/env.h>

#include "Lin-DB/util/coding.h"
#include "Lin-DB/util/crc32c.h"

namespace lindb {
namespace log {
namespace {

constexpr unsigned int kEof = kMaxRecordType + 1;
constexpr unsigned int kBadRecord = kMaxRecordType + 2;

}

Reader::Reader(SequentialFile* file, bool checksum)
    : file_(file), checksum_(checksum), buffer_(), eof_(false) {}

bool Reader::ReadRecord(std::string* record, Status* status) {
    record->clear();
    *status = Status::OK();
    // 暂存 FIRST/MIDDLE/LAST 拼接内容
    std::string scratch;
    // 标记是否正在拼接一个 fragmented logical
    bool in_fragmented_record = false;

    while (true) {
        Slice fragment;
        const unsigned int type = ReadPhysicalRecord(&fragment, status);

        switch (type) {
        case kFullType:
            record->assign(fragment.data(), fragment.size());
            return true;
           
        case kFirstType:
            scratch.assign(fragment.data(), fragment.size());
            in_fragmented_record = true;
            break;

        case kMiddleType:
            if (!in_fragmented_record) {
                *status = Status::Corruption("missing start of fragmented WAL record");
                return false;
            }
            scratch.append(fragment.data(), fragment.size());
            break;

        case kLastType:
            if (!in_fragmented_record) {
                *status = Status::Corruption("missing start of frafmented WAL record");
                return false;
            }
            scratch.append(fragment.data(), fragment.size());
            record->swap(scratch);
            return true;

        case kBadRecord:
            return false;

        case kEof:
            if (in_fragmented_record) {
                *status = Status::Corruption("partial WAL record without LAST");
            }
            return false;
        
        default:
            *status = Status::Corruption("unknown WAL reader state");
            return false;
        }
    }
}

unsigned int Reader::ReadPhysicalRecord(Slice* result, Status* status) {
    while (true) {
        if (buffer_.size() < kHeaderSize) {
            return kEof;
        }

        Slice block;
        Status read_status = file_->Read(kBlockSize, &block, backing_store_);
        if (!read_status.ok()) {
            *status = read_status;
            eof_ = true;
            return kBadRecord;
        }

        buffer_ = block;
        if (buffer_.empty()) {
            eof_ = true;
            return kEof;
        }

        if (buffer_.size() < kHeaderSize) {
            eof_ = true;
            *status = Status::Corruption("truncated WAL record header");
            return kBadRecord;
        }
    }

    const char* header = buffer_.data();
    const uint32_t stored_crc = DecodeFixed32(header);
    const uint32_t length = static_cast<unsigned char>(header[4]) | (static_cast<uint32_t>(static_cast<unsigned char>(header[5])) << 8);
    const unsigned int type = static_cast<unsigned char>(header[6]);

    if (kHeaderSize + length > buffer_.size()) {
        buffer_.clear();
        if (!eof_) {
            *status = Status::Corruption("bad WAL record length");
            return kBadRecord;
        }
        return kEof;
    }

    if (type == kZeroType && length == 0) {
        buffer_.clear();
        return kBadRecord;
    }

    if (type > kMaxRecordType) {
        *status = Status::Corruption("unknown WAL record type");
        return kBadRecord;
    }

    const char* payload = header + kHeaderSize;
    if (checksum_) {
        uint32_t actual_crc = crc32c::Value(header + 6, 1);

        if (crc32c::Unmask(stored_crc) != actual_crc) {
            buffer_.remove_prefix(kHeaderSize + length);
            *status = Status::Corruption("WAL record checksum mismatch");
            return kBadRecord;
        }
    }

    *result = Slice(payload, length);
    buffer_.remove_prefix(kHeaderSize + length);
    return type;
}

}


}