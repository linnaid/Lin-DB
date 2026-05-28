// FilterBlockBuilder(写入时生成 filter) + FilterBlockReader(读取时查询)
// 定义 SSTable filter block 的构建和读取接口
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <lindb/slice.h>

namespace lindb {

class FilterPolicy;

class FilterBlockBuilder {
public:
    explicit FilterBlockBuilder(const FilterPolicy* policy);
    FilterBlockBuilder(const FilterBlockBuilder&) = delete;
    FilterBlockBuilder& operator=(const FilterBlockBuilder&) = delete;

    void StartBlock(uint64_t block_offset);
    void AddKey(const Slice& key);
    Slice Finish();

private:
    // 根据添加的 key 生成 filter 并将其追加到result_
    void GenerateFilter();

    const FilterPolicy* policy_;
    std::string keys_;
    std::vector<size_t> start_;
    std::string result_;
    std::vector<Slice> tmp_keys_;
    std::vector<uint32_t> filter_offsets_;
};

class FilterBlockReader {
public:
    FilterBlockReader(const FilterPolicy* policy, const Slice& contents);

    bool KeyMayMatch(uint64_t block_offset, const Slice& key);

private:
    const FilterPolicy* policy_;
    const char* data_;
    const char* offset_;
    size_t num_;
    size_t base_lg_;
};

}

// Filter block 格式：
// +--------------------+
// | filter 0           |
// +--------------------+
// | filter 1           |
// +--------------------+
// | filter 2           |
// +--------------------+
// | ...                |
// +--------------------+
// | offset[0]          |
// +--------------------+
// | offset[1]          |
// +--------------------+
// | offset[2]          |
// +--------------------+
// | ...                |
// +--------------------+
// | array_offset       | 4 bytes
// +--------------------+
// | base_lg            | 1 byte
// +--------------------+