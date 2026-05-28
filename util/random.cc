#include "Lin-DB/util/random.h"
#include <cassert>

namespace lindb {

// 这里使用一个简单的 31 位线性同余/乘法型伪随机数生成器
Random::Random(uint32_t s)
    : seed_(s & 0x7fffffffu) {
        if (seed_ == 0 || seed_ == 2147483647L) {
            seed_ = 1;
        }
    }

uint32_t Random::Next() {
    // M = 2^31 - 1, 一个梅森素数
    static const uint32_t kM = 2147483647L;

    // A = 16807, 是 Park-Miller 经典参数
    static const uint64_t kA = 16807;

    const uint64_t product = seed_ * kA;
    seed_ = static_cast<uint32_t>((product >> 31) + (product & kM));

    if (seed_ > kM) {
        seed_ -= kM;
    }

    return seed_;
}

uint32_t Random::Uniform(int n) {
    assert(n > 0);

    return Next() % n;
}

bool Random::OneIn(int n) {
    assert(n > 0);

    return (Next() % n) == 0;
}

uint32_t Random::Skewed(int max_log) {
    return Uniform(1 << Uniform(max_log + 1));
}

}