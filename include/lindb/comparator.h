// 定义数据库全局比较规则
#pragma once

namespace lindb {

class Slice;

class Comparator {
public:
    virtual ~Comparator() = default;

    // 返回顺序关系
    virtual int Compare(const Slice& a, const Slice& b) const = 0;

    // 返回比较器名字(持久化磁盘时，用于校验打开DB的比较器是否一致)
    virtual const char* Name() const = 0;

    // 在 start < limit 时尽量把 start 缩短，给 index block 降体积
    virtual void FindShortestSeparator(std::string* start, const Slice& limit) const = 0;

    // 把 key 缩成一个更短但不小于原值的候后继 key
    virtual void FindShortSuccessor(std::string* key) const = 0;
};

// 默认的字节序比较器(按字典序比较 user key)
const Comparator* BytewiseComparator();

}