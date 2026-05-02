// 提供 Level-DB 风格 32-bit hash，后续 BloomFilter 会依赖它
#pragma once

#include <cstddef>
#include <cstdint>

namespace lindb {

// 基于 data、长度和 seed 计算稳定 32-bit hash
uint32_t Hash(const char* data, size_t n, uint32_t seed);

}