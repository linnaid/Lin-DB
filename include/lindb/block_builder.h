// 定义 SSTable 最小写入单元，把一串有序 key/value 变成 LevelDB 风格 block
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <lindb/slice.h>

namespace lindb {

struct Options;

// 把有序 key 编码成一个 block
class BlockBuilder {
public:
    explicit BlockBuilder(const Options* options);
    BlockBuilder(const BlockBuilder&) = delete;
    BlockBuilder& operator=(const BlockBuilder&) = delete;

    // 清空当前 block，回到刚构造完成的状态
    void Reset();
    void Add(const Slice& key, const Slice& value);
    // 写入 restart array 和 restart 个数，返回最终 block 内容
    Slice Finish();
    // 估算当前若 finish， block 完成后的大小
    size_t CurrentSizeEstimate() const;
    bool empty() const { return buffer_.empty(); }

private:
    const Options* options_;
    std::string buffer_;
    std::vector<uint32_t> restarts_;
    int counter_;
    bool finished_;
    std::string last_key_;
};

}