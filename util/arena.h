// 负责 "编码好的东西放到哪里，并且地址永远不变"
// 内存分配器(内存池)
#pragma once

#include <cstddef>
#include <vector>
#include <cassert>
#include <atomic>

namespace lindb {
class Arena {
public:
    Arena();

    ~Arena();

    // 普通分配路径，给 entry 字节串这种连续缓存区用 
    // 从当前 block 顺序切一段，不够走 fallback
    char* Allocate(size_t bytes);

    // 对齐分配路径，给 SkipList::Node 这种带指针/原子成员的结构体用，避免未对齐
    char* AllocateAligned(size_t bytes);

    // 返回整个 arena 申请过的总块大小近似值
    // 后面给 memtable 控制内存上限用
    size_t MemoryUsage() const;

private:
    // 当前 block 放不下时的兜底逻辑
    // 小对象新开标准 block，大对象单独开块，避免浪费太大尾部碎片
    char* AllocateFallback(size_t bytes);
    // 真正向系统申请一整块内存，并记录到 blocks_，析构时统一释放
    char* AllocateNewBlock(size_t block_bytes);

    char* alloc_ptr_;
    size_t alloc_bytes_remaining_;

    std::vector<char*> blocks_;
    std::atomic<size_t> memory_usage_;
};

inline size_t Arena::MemoryUsage() const {
    return memory_usage_.load(std::memory_order_relaxed);
}

inline char* Arena::Allocate(size_t bytes) {
    assert(bytes > 0);

    if (bytes <= alloc_bytes_remaining_) {
        char* result = alloc_ptr_;
        alloc_ptr_ += bytes;
        alloc_bytes_remaining_ -= bytes;
        return result;
    }

    return AllocateFallback(bytes);
}

}