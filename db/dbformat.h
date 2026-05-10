// 负责 "key 的排序和查找语义"
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <lindb/slice.h>
#include <lindb/comparator.h>

namespace lindb {

// 版本号类型(区分先后，越大越新)
using SequenceNumber = uint64_t;

// value 类型
enum ValueType : uint8_t {
    kTypeDeletion = 0x0,
    kTypeValue = 0x1,
};

// seek 时使用的最大类型
// 作用：internal key 比较时 seq 是降序，相同 seq 下type 也参与排序，
// 所以查找某个 seq 时要带一个足够大的 type，确保 seek 到该seq可见的第一个版本； 
constexpr ValueType kValueTypeForSeek = kTypeValue;

// suqence 的最大值
constexpr SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);

// 解析后的 InternalKey(将二进制 key 转换成更易操作的三元组)
// 定义"逻辑视图"
struct ParsedInternalKey {
    // 用户传进来的key
    Slice user_key;

    // 版本号
    SequenceNumber sequence;

    // 值类型
    ValueType type;

    ParsedInternalKey();
    ParsedInternalKey(const Slice& u, SequenceNumber seq, ValueType t);

};

// 把 sequence 和 type 打包成一个64位 tag
uint64_t PackSequenceAndType(SequenceNumber seq, ValueType type);

// 从 internalkey 中提取 userkey
Slice ExtractUserKey(const Slice& internal_key);

// 返回 ParsedInternalKey 编码后的长度(预估 buffer 大小)
size_t InternalKeyEncodingLength(const ParsedInternalKey& key);

// 把 ParsedInternalKey 编码追加到结果字符串(构造 internal key 的统一入口)
void AppendInternalKey(std::string* result, const ParsedInternalKey& key);

// 解析一个编码后的 internal key(把二进制key还原成 userkey + seq + type)
bool ParseInternalKey(const Slice& internal_key, ParsedInternalKey* result);

// InternalKey 是 internal key 的拥有型封装
// (避免用std::string 当作 interkey，用错比较器)
class InternalKey {
public:
    // 空 key， 表示未初始化或无效状态
    InternalKey();

    // 构造internalkey
    InternalKey(const Slice& user_key, SequenceNumber seq, ValueType type);

    // 直接返回编码后的key
    Slice Encode() const;

    // 从编码后的 Slice 复制出一个 internal key
    bool DecodeFrom(const Slice& encoded);

    // 但会 user key 的部分
    Slice user_key() const;

    // 用解析后的结构重建内部编码
    void SetFrom(const ParsedInternalKey& key);

    // 清空当前 key
    void Clear();

private:
    std::string rep_;
};

// InternalKeyComparator 定义 internal key 的总排序
// 先按 user key 升序；user key 相同则按 sequence 降序；必要时再按 type 
class InternalKeyComparator : public Comparator {
public:
    explicit InternalKeyComparator(const Comparator* user_comparator);

    // 比较两个编码后的 internal key
    int Compare(const Slice& a, const Slice& b) const override;

    // 返回比较器的名字
    const char* Name() const override;

    void FindShortestSeparator(std::string* start, const Slice& limit) const override;

    void FindShortSuccessor(std::string* key) const override;
    
    // 返回第层 user key comparator
    const Comparator* user_comparator() const;

    // 比较两个封装后的 InternalKey
    int Compare(const InternalKey& a, const InternalKey& b) const;
private:
    const Comparator* user_comparator_;
};

// 读路径用来查找 key(构造一个"再某个 sequence 视角下查 user key"的目标 key)
// 让 memtable 可以一次 seek 定位到可见的最新版本
class LookupKey {
public:
    // 为 user_key + sequence 构造查找 key 
    explicit LookupKey(const Slice& user_key, SequenceNumber sequence);

    LookupKey(const LookupKey&) = delete;
    LookupKey& operator=(const LookupKey&) = delete;

    ~LookupKey();

    // 返回适合 memtable 查找的完整编码
    Slice memtable_key() const;

    // 返回其中的 internal key 部分
    Slice internal_key() const;

    // 返回原始 user key
    Slice user_key() const;

private:
    // 指向完整缓冲区起始位置
    const char* start_;

    // 指向 internal key 起始位置
    const char* kstart_;

    // 指向缓冲区结束位置
    const char* end_;
    
    // 小 key, 走栈上缓冲区，减少堆分配
    char space_[200];
};

}
