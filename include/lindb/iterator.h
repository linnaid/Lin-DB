// 把所有可遍历数据源的公共协议先定下来
#pragma once

#include <cassert>
#include <lindb/slice.h>
#include <lindb/status.h>


namespace lindb {

class Iterator {
public:
    using CleanupFunction = void (*)(void* arg1, void* arg2);

    // 构造一个迭代器基类对象
    Iterator();
    virtual ~Iterator();

    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;

    // 当前是否指向一个有效 entry
    virtual bool Valid() const = 0;
    // 定位到第一个key
    virtual void SeekToFirst() = 0;
    virtual void SeekToLast() = 0;
    virtual void Seek(const Slice& target) = 0;
    virtual void Next() = 0;
    virtual void Prev() = 0;
    virtual Slice key() const = 0;
    virtual Slice value() const = 0;
    virtual Status status() const = 0;

    // 注册析构时要执行的清理工作
    void RegisterCleanup(CleanupFunction function, void* arg1, void* arg2);

private:
    // cleanup 单链表节点
    struct CleanupNode {
        bool IsEmpty() const { return function == nullptr; }
        // 执行当前节点上的 cleanup
        void Run() {
            assert(function != nullptr);
            (*function)(arg1, arg2);
        }

        CleanupFunction function;
        void* arg1;
        void* arg2;
        CleanupNode* next;
    };

    CleanupNode cleanup_head_;
};

Iterator* NewEmptyIterator();
Iterator* NewErrorIterator(const Status& status);

}