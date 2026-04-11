#include "util/coding.h"

namespace lindb {
    void EncodeFixed64(char* dst, uint64_t value) {
        auto* buffer = reinterpret_cast<uint8_t*>(dst);
        buffer[0] = static_cast<uint8_t>(value);
        buffer[1] = static_cast<uint8_t>(value >> 8);
        buffer[2] = static_cast<uint8_t>(value >> 16);
        buffer[3] = static_cast<uint8_t>(value >> 24);
        buffer[4] = static_cast<uint8_t>(value >> 32);
        buffer[5] = static_cast<uint8_t>(value >> 40);
        buffer[6] = static_cast<uint8_t>(value >> 48);
        buffer[7] = static_cast<uint8_t>(value >> 56);
    }

    void PutFixed64(std::string* dst, uint64_t value) {
        char buf[sizeof(value)];
        EncodeFixed64(buf, value);
        dst->append(buf, sizeof(buf));        
    }

    uint64_t DecodeFixed64(const char* ptr) {
        const auto* buffer = reinterpret_cast<const uint8_t*>(ptr);
        return static_cast<uint64_t>(buffer[0]) |
               (static_cast<uint64_t>(buffer[1]) << 8) |
               (static_cast<uint64_t>(buffer[2]) << 16) |
               (static_cast<uint64_t>(buffer[3]) << 24) |
               (static_cast<uint64_t>(buffer[4]) << 32) |
               (static_cast<uint64_t>(buffer[5]) << 40) |
               (static_cast<uint64_t>(buffer[6]) << 48) |
               (static_cast<uint64_t>(buffer[7]) << 56);
    }

    char* EncodeVarint32(char* dst, uint32_t value) {
        static const uint32_t kContinuationBit = 128;
        auto* ptr = reinterpret_cast<uint8_t*>(dst);
        while (value >= kContinuationBit) {
            *(ptr++) = static_cast<uint8_t>((value & 0x7f) | kContinuationBit);
            value >>= 7;
        }
        *(ptr++) = static_cast<uint8_t>(value);
        return reinterpret_cast<char*>(ptr);
    }

    const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* value) {
        uint32_t result = 0;
        for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
            const uint32_t byte = *(reinterpret_cast<const uint8_t*>(p));
            ++p;
            if((byte & 128) != 0) {
                result |= ((byte & 127) << shift);
            } else {
                result |= (byte << shift);
                *value = result;
                return p;
            }
        }
        return nullptr;
    }

    void PutVarint32(std::string* dst, uint32_t value) {
        char buf[5];
        char* ptr = EncodeVarint32(buf, value);
        dst->append(buf, ptr - buf);
    }

    int VarintLength(uint64_t value) {
        int len = 1;
        while(value >= 128) {
            value >>= 7;
            len++;
        }
        return len;
    }

}