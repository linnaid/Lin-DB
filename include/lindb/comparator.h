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
    };

    // 默认的字节序比较器(按字典序比较 user key)
    const Comparator* BytewiseComparator();
}