#include "Lin-DB/db/format.h"

#include <cassert>

#include "lindb/env.h"
#include "lindb/options.h"
#include "Lin-DB/util/coding.h"
#include "Lin-DB/util/crc32c.h"

namespace lindb {

void BlockHandle::EncodeTo(std::string* dst) const {
    assert(offset_ != ~static_cast<uint64_t>(0));
    assert(size_ != ~static_cast<uint64_t>(0));
    PutVarint64(dst, offset_);
    PutVarint64(dst, size_);
}

Status BlockHandle::DecodeFrom(Slice* input) {
    if (GetVarint64(input, &offset_) && GetVarint64(input, &size_)) {
        return Status::OK();
    }
    return Status::Corruption("bad block handle");
}

void Footer::EncodeTo(std::string* dst) const {
    const size_t original_size = dst->size();
    metaindex_handle_.EncodeTo(dst);
    index_handle_.EncodeTo(dst);
    //////////////////////////////////////////////
    // 原本kMaxEncodedLength 
    dst->resize(original_size + 2 * BlockHandle::kMaxEncodedLength);
    // 写 magic
    PutFixed32(dst, static_cast<uint32_t>(kTableMagicNumber));
    PutFixed32(dst, static_cast<uint32_t>(kTableMagicNumber >> 32));
}

Status Footer::DecodeFrom(Slice* input) {
    if (input->size() < kEncodedLength) {
        return Status::Corruption("not an sstable (footer too short)");
    }

    const char* magic_ptr = input->data() + kEncodedLength - 8;
    const uint32_t magic_lo = DecodeFixed32(magic_ptr);
    const uint32_t magic_hi = DecodeFixed32(magic_ptr + 4);
    const uint64_t magic = (static_cast<uint64_t>(magic_hi) << 32) | static_cast<uint64_t>(magic_lo);

    if (magic != kTableMagicNumber) {
        return Status::Corruption("not an sstable (bad magic number)");
    }

    Status s = metaindex_handle_.DecodeFrom(input);
    if (s.ok()) {
        s = index_handle_.DecodeFrom(input);
    }
    if (s.ok()) {
        const char* end = magic_ptr + 8;
        *input = Slice(end, input->data() + input->size() - end);
    }
    return s;
}

Status ReadBlock(RandomAccessFile* file, const ReadOptions& options, 
    const BlockHandle& handle, BlockContents* result) {
        result->data = Slice();
        result->cachable = false;
        result->heap_allocated = false;

        const size_t n = static_cast<size_t>(handle.size());
        char* buf = new char[n + kBlockTrailerSize];
        Slice contents;
        Status s = file->Read(handle.offset(), n + kBlockTrailerSize, &contents, buf);
        if (!s.ok()) {
            delete[] buf;
            return s;
        }

        if (contents.size() != n + kBlockTrailerSize) {
            delete[] buf;
            return Status::Corruption("truncated block read");
        }

        const char* data = contents.data();
        if (options.verify_checksums) {
            const uint32_t stored = crc32c::Unmask(DecodeFixed32(data + n + 1));
            const uint32_t actual = crc32c::Value(data, n + 1);
            if (stored != actual) {
                delete[] buf;
                return Status::Corruption("block checksum mismatch");
            }
        }

        // 我还未实现压缩，所以暂时不支持压缩块
        if (data[n] != kNoCompression) {
            delete[] buf;
            return Status::NotSupported("compressed block");
        }

        if (data != buf) {
            delete[] buf;
            result->data = Slice(data, n);
            result->heap_allocated = false;
            result->cachable = false;
        } else {
            result->data = Slice(buf, n);
            result->heap_allocated = true;
            result->cachable = true;
        }
        
        return Status::OK();
    }

}