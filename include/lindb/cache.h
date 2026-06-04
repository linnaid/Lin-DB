// Cache 抽象接口 + NewLRUCache 工厂
// 对外暴露缓存能力，Table/DB 不依赖具体 LRU 实现
// handle 时不透明指针，调用方无法直接操作，只能通过接口访问
#pragma once

#include <cstddef>
#include <cstdint>
#include <lindb/slice.h>

namespace lindb {

class Cache {
public:
    struct Handle {};

    using Deleter = void (*)(const Slice& key, void* value);

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    virtual ~Cache();

    // charge 代表该条目占用的内存大小，用于 LRU 淘汰计算
    // 被淘汰或删除时调用 deleter 回调函数释放
    virtual Handle* Insert(const Slice& key, void* value, size_t charge, Deleter deleter) = 0;
    virtual Handle* Lookup(const Slice& key) = 0;
    virtual void Release(Handle* handle) = 0;
    virtual void Erase(const Slice& key) = 0;
    virtual void* Value(Handle* handle) = 0;
    virtual uint64_t NewId() = 0;
};

Cache* NewLRUCache(size_t capacity);

}