// 定义 SSTable 最小读取单元，对外暴露 NewIterator
#pragma once

#include <cstddef>
#include <cstdint>

#include <lindb/iterator.h>

namespace lindb {

class Comparator;

struct BlockContents {
    Slice data;
    bool cachable = false;
    bool heap_allocated = false;
};

class Block {
public:
    explicit Block(const BlockContents& contents);
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    ~Block();

    size_t size() const { return size_; }
    Iterator* NewIterator(const Comparator* comparator);

private:
    uint32_t NumRestarts() const;

    const char* data_;
    size_t size_;
    uint32_t restart_offset_;
    bool owned_;
};

}