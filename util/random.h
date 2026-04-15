#pragma once

#include <cstdint>

namespace lindb {

class Random {
public:

    // 构造随机数发生器
    explicit Random(uint32_t s);

    // 生成下一个伪随机数
    uint32_t Next();

    // 返回[0, n-1] 范围内的一个均匀随机数
    uint32_t Uniform(int n);

    // 以大约 1/n 的概率返回
    bool OneIn(int n);

    // 返回一个偏向较小值的随机数
    uint32_t Skewed(int max_log);

private:
    uint32_t seed_;
};

}