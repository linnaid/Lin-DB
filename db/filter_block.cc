#include "Lin-DB/db/filter_block.h"

#include <cassert>

#include <lindb/filter_policy.h>

#include "Lin-DB/util/coding.h"

namespace lindb {

namespace {

// 表示一个 filter 覆盖多少字节的数据(eg. 2^11 = 2048 = 2KB)
constexpr size_t kFilterBaseLg = 11;
constexpr size_t kFilterBase = 1 << kFilterBaseLg;

}

FilterBlockBuilder::FilterBlockBuilder(const FilterPolicy* policy) 
    : policy_(policy) {}

void FilterBlockBuilder::StartBlock(uint64_t block_offset) {
    const uint64_t filter_index = block_offset / kFilterBase;
    assert(filter_index >= filter_offsets_.size());
    while (filter_index > filter_offsets_.size()) {
        GenerateFilter();
    }
}

void FilterBlockBuilder::AddKey(const Slice& key) {
    start_.push_back(keys_.size());
    keys_.append(key.data(), key.size());
}

Slice FilterBlockBuilder::Finish() {
    if (!start_.empty()) {
        GenerateFilter();
    }

    const uint32_t array_offset = static_cast<uint32_t>(result_.size());
    for (size_t i = 0; i < filter_offsets_.size(); ++i) {
        PutFixed32(&result_, filter_offsets_[i]);
    }

    PutFixed32(&result_, array_offset);
    result_.push_back(static_cast<char>(kFilterBaseLg));
    return Slice(result_);
}

void FilterBlockBuilder::GenerateFilter() {
    const size_t num_keys = start_.size();
    if (num_keys == 0) {
        filter_offsets_.push_back(static_cast<uint32_t>(result_.size()));
        return;
    }

    start_.push_back(keys_.size());
    tmp_keys_.resize(num_keys);
    for (size_t i = 0; i < num_keys; ++i) {
        const char* base = keys_.data() + start_[i];
        const size_t length = start_[i + 1] - start_[i];
        tmp_keys_[i] = Slice(base, length);
    }

    filter_offsets_.push_back(static_cast<uint32_t>(result_.size()));
    policy_->CreateFilter(tmp_keys_.data(), static_cast<int>(num_keys), &result_);

    tmp_keys_.clear();
    keys_.clear();
    start_.clear();
}

FilterBlockReader::FilterBlockReader(const FilterPolicy* policy, const Slice& contents)
    : policy_(policy), data_(nullptr), offset_(nullptr), num_(0), base_lg_(0) {
        const size_t n = contents.size();
        if (n < 5) {
            return;
        }

        base_lg_ = static_cast<unsigned char>(contents.data()[n - 1]);
        const uint32_t array_offset = DecodeFixed32(contents.data() + n - 5);
        if (array_offset > n - 5) {
            return;
        }

        data_ = contents.data();
        offset_ = data_ + array_offset;
        num_ = (n - 5 - array_offset) / 4;
    }

bool FilterBlockReader::KeyMayMatch(uint64_t block_offset, const Slice& key) {
    if (data_ == nullptr || offset_ == nullptr) {
        return true;
    }

    const uint64_t index = block_offset >> base_lg_;
    if (index < num_) {
        const uint32_t start = DecodeFixed32(offset_ + index * 4);
        const uint32_t limit = DecodeFixed32(offset_ + index * 4 + 4);
        if (start == limit) {
            return false;
        }
        if (start < limit && limit <= static_cast<size_t>(offset_ - data_)) {
            const Slice filter(data_ + start, limit - start);
            return policy_->KeyMayMatch(key, filter);
        }
    }
    return true;
}

}