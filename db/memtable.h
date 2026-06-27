// 内存写缓冲区，负责暂存所有尚未持久化到 SSTable 的数据
#pragma once

#include <string>

#include <lindb/status.h>

#include "Lin-DB/db/dbformat.h"
#include "Lin-DB/db/skiplist.h"
#include "Lin-DB/util/arena.h"

namespace lindb {

class Iterator;

// 封装内存中的有序多版本表
class MemTable {
public:
    explicit MemTable(const InternalKeyComparator& comparator);
    MemTable(const MemTable&) = delete;
    MemTable& operator=(const MemTable&) = delete;

    // 在这里插入entry
    void Add(SequenceNumber seq, ValueType type, const Slice& key, const Slice& value);
    bool Get(const LookupKey& key, std::string* value, Status* status);
    bool Get(const LookupKey& key, std::string* value);

    // 返回一个遍历 internal key -> value 的统一迭代器
    Iterator* NewIterator() const;

    // 返回当前 Arena 近似占用的内存
    size_t ApproximateMemoryUsage() const;

    // 判断当前 MemTable 是否没有任何真实写入 entry，供 recovery 判断是否需要 final flush
    bool Empty() const;

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

    // 前向声明 Memtable 专用迭代器实现
    class MemTableIterator;

    // 保存 MemTable 中所有 entry 和 skiplist node 的内存
    Arena arena_;

    // 比较器对象
    // 注：必须在 table_ 前声明，因为 table_ 构造时需要引用它
    KeyComparator comparator_;

    // 内部有序链表。每个节点保存一个 const char*，指向 Arena 里的 entry
    Table table_;
};

}

// MemTable 中每条记录：
// +---------------------------+
// | klength(varint32)         |
// +---------------------------+
// | userkey                   |
// +---------------------------+
// | tag(seq+type,8B)          |
// +---------------------------+
// | vlength(varint32)         |
// +---------------------------+
// | value                     |
// +---------------------------+