#include "Lin-DB/util/crc32c.h"

#include <array>
#include <cstdint>

namespace lindb {
namespace crc32c {
namespace {

// CRC32C Castagnoli 反射多项式
constexpr uint32_t kCRC32CPolynomial = 0x82f63b78U;
// CRC mask 使用的固定偏移量
constexpr uint32_t kMaskDelta = 0xa282ead8U;

std::array<uint32_t, 256> MakeTable() {
    std::array<uint32_t, 256> table{};
    
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 1U) != 0) {
                crc = (crc >> 1) ^ kCRC32CPolynomial;
            }  else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }

    return table;
}

const std::array<uint32_t, 256>& Table() {
    static const std::array<uint32_t, 256> table = MakeTable();
    return table;
}

}

uint32_t Extend(uint32_t init_crc, const char* data, size_t n) {
    uint32_t crc = init_crc ^ 0xffffffffU;
    const auto* ptr = reinterpret_cast<const uint8_t*>(data);
    const auto& table = Table();

    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ ptr[i]) & 0xffU] ^ (crc >> 8);
    }

    return crc ^ 0xffffffffU;
}

uint32_t Value(const char* data, size_t n) {
    return Extend(0, data, n);
}

uint32_t Mask(uint32_t crc) {
    return ((crc >> 15) | (crc << 17)) + kMaskDelta;
}

uint32_t Unmask(uint32_t masked_crc) {
    const uint32_t rotated = masked_crc - kMaskDelta;
    return ((rotated >> 17) | (rotated << 15));
}

}

}