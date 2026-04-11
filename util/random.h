#pragma once

#include <cstdint>

namespace lindb {

class Random {
public:
    explicit Random(uint32_t s);
    uint32_t Next();
    uint32_t Uniform(int n);
    bool OneIn(int n);
    uint32_t Skewed(int max_log);

private:
    uint32_t seed_;
};

}