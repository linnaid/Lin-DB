#include "Lin-DB/util/hash.h"

#include <cstddef>
#include <cstdint>

#include "Lin-DB/util/coding.h"


namespace lindb {

uint32_t Hash(const char* data, size_t n, uint32_t seed) {
    // hash 混合乘法常量
    constexpr uint32_t m = 0xc6a4a793U;
    // hash 混合右移位数
    constexpr uint32_t r = 24;

    const char* limit = data + n;
    uint32_t h = seed ^ static_cast<uint32_t>(n * m);

    while (data + 4 <= limit) {
        uint32_t word = DecodeFixed32(data);
        h += word;
        h *= m;
        h ^= (h >> 16);
        data += 4;
    }

    switch (limit - data) {
    case 3:
        h += static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 16;
        [[fallthrough]];
    case 2:
        h += static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 8;
        [[fallthrough]];
    case 1:
        h += static_cast<uint32_t>(static_cast<uint8_t>(data[0]));
        
        h *= m;
        h ^= (h >> r);
        break;
    default:
        break;
    }
}


}