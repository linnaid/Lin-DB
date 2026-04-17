// 内存写缓冲区，负责暂存所有尚未持久化到 SSTable 的数据
#pragma once

#include "Lin-DB/db/dbformat.h"
#include "Lin-DB/db/skiplist.h"
#include "Lin-DB/util/arena.h"

namespace lindb {

class MemTable {
public:
    explicit MemTable(const InternalKeyComparator& comparator);
    MemTable(const MemTable&) = delete;
    MemTable& operator=(const MemTable&) = delete;

    void Add(SequenceNumber seq, ValueType type, const Slice& key, const Slice& value);
    bool Get(const LookupKey& key, std::string* value);

    // 返回当前 Arena 近似占用的内存
    size_t ApproximateMemoryUsage() const;

private:
    struct KeyComparator {
        // 保存 internal key comparator 
        explicit KeyComparator(const InternalKeyComparator& Comparator);

        // 比较两个 entry
        int operator()(const char* a, const char* b) const;

        const InternalKeyComparator comparator;
    };

    // 给模板类型起别名，使得代码看起来更简洁
    using Table = SkipList<const char*, KeyComparator>;

    // 保存 MemTable 中所有 entry 和 skiplist node 的内存
    Arena arena_;

    // 比较器对象
    // 注：必须在 table_ 前声明，因为 table_ 构造时需要引用它
    KeyComparator comparator_;

    // 内部有序链表。每个节点保存一个 const char*，指向 Arena 里的 entry
    Table table_;
};


}