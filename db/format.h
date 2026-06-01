// 定义 SSTable 文件尾和 block 定位格式，是 TableBuilder 和后面 Table::Open 的共同协议
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <lindb/block.h>
#include <lindb/status.h>

namespace lindb {

class RandomAccessFile;
struct ReadOptions;

// 描述单个 block 在文件里的位置和长度
class BlockHandle {
public:
    enum { kMaxEncodedLength = 10 + 10 };

    BlockHandle();

    uint64_t offset() const { return offset_; }
    void set_offset(uint64_t offset) { offset_ = offset; }

    uint64_t size() const { return size_; }
    void set_size(uint64_t size) { size_ = size; }

    void EncodeTo(std::string* dst) const;
    Status DecodeFrom(Slice* input);

private:
    uint64_t offset_;
    uint64_t size_;
};

class Footer {
public:
    enum { kEncodedLength = 2 * BlockHandle::kMaxEncodedLength + 8 };

    Footer() = default;
    
    const BlockHandle& metaindex_handle() const { return metaindex_handle_; }
    void set_metaindex_handle(const BlockHandle& h) { metaindex_handle_ = h; }

    const BlockHandle& index_handle() const { return index_handle_; }
    void set_index_handle(const BlockHandle& h) { index_handle_ = h; }

    void EncodeTo(std::string* dst) const;
    Status DecodeFrom(Slice* input);

private:
    BlockHandle metaindex_handle_;
    BlockHandle index_handle_;
};

inline constexpr uint64_t kTableMagicNumber = 0xdb4775248b80fb57ull;
// 1 byte type + 4 byte masked crc
inline constexpr size_t kBlockTrailerSize = 5;

Status ReadBlock(RandomAccessFile* file, const ReadOptions& options, const BlockHandle& handle, BlockContents* result);

inline BlockHandle::BlockHandle() 
    : offset_(~static_cast<uint64_t>(0)),
      size_(~static_cast<uint64_t>(0)) {}

}

// BlockHandle 格式：
// +----------------+
// | offset(varint) |
// +----------------+
// | size(varint)   |
// +----------------+

// Footer 格式：
// +----------------------+
// | metaindex handle     |
// +----------------------+
// | index handle         |
// +----------------------+
// | padding              |
// +----------------------+
// | magic number(8B)     |
// +----------------------+