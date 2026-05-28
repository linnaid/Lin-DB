// FilterPolicy 抽象接口 + NewBloomFilterPolicy 工厂
#pragma once

#include <string>

namespace lindb {

class Slice;

class FilterPolicy {
public:
    virtual ~FilterPolicy();

    virtual const char* Name() const = 0;

    virtual void CreateFilter(const Slice* keys, int n, std::string* dst) const = 0;

    virtual bool KeyMayMatch(const Slice& key, const Slice& filter) const = 0;
};

const FilterPolicy* NewBloomFilterPolicy(int bits_per_key);

}