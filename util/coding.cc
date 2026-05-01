#include "util/coding.h"

#include <cstddef>
#include <limits>
#include <cassert>

namespace lindb {

// varint 每个字节最高位表示“后面还有字节”
constexpr uint8_t kVarintContinuationBit = 128;  
// varint 每个字节低 7 bit保存实际数值
constexpr uint8_t kVarintValueMask = 127;  

void EncodeFixed32(char* dst, uint32_t value) {
    auto* buffer = reinterpret_cast<unsigned char*>(dst);
    buffer[0] = static_cast<unsigned char>(value);
    buffer[1] = static_cast<unsigned char>(value >> 8);
    buffer[2] = static_cast<unsigned char>(value >> 16);
    buffer[3] = static_cast<unsigned char>(value >> 24);
}

void PutFixed32(std::string* dst, uint32_t value) {
    char buffer[sizeof(value)];
    EncodeFixed32(buffer, value);
    dst->append(buffer, sizeof(buffer));
}

uint32_t DecodeFixed32(const char* ptr) {
    const auto* buffer = reinterpret_cast<const unsigned char*>(ptr);
    return static_cast<uint32_t>(buffer[0]) |
            (static_cast<uint32_t>(buffer[1]) << 8) |
            (static_cast<uint32_t>(buffer[2]) << 16) |
            (static_cast<uint32_t>(buffer[3]) << 24);
}

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
    auto* ptr = reinterpret_cast<uint8_t*>(dst);
    while (value >= kVarintContinuationBit) {
        *(ptr++) = static_cast<uint8_t>((value & kVarintValueMask) | kVarintContinuationBit);
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
        if((byte & kVarintContinuationBit) != 0) {
            result |= ((byte & kVarintValueMask) << shift);
        } else {
            result |= (byte << shift);
            *value = result;
            return p;
        }
    }
    return nullptr;
}

bool GetVarint32(Slice* input, uint32_t* value) {
    const char* start = input->data();
    const char* limit = start + input->size();
    const char* parsed = GetVarint32Ptr(start, limit, value);
    if (parsed == nullptr) {
        return false;
    }
    input->remove_prefix(static_cast<size_t>(parsed - start));
    return true;
}

void PutVarint32(std::string* dst, uint32_t value) {
    char buf[5];
    char* ptr = EncodeVarint32(buf, value);
    dst->append(buf, ptr - buf);
}


char* EncodeVarint64(char* dst, uint64_t value) {
    auto* ptr = reinterpret_cast<uint8_t*>(dst);
    while (value >= kVarintContinuationBit) {
        *(ptr++) = static_cast<uint8_t>((value & kVarintValueMask) | kVarintContinuationBit);
        value >>= 7;
    }
    *(ptr++) = static_cast<uint8_t>(value);
    return reinterpret_cast<char*>(ptr);
}

void PutVarint64(std::string* dst, uint64_t value) {
    char buffer[10];
    char* end = EncodeVarint64(buffer, value);
    dst->append(buffer, static_cast<size_t>(end - buffer));
}

const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
    uint64_t result = 0;
    for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7) {
        const uint64_t byte = static_cast<uint8_t>(*p);
        ++p;
        if ((byte & kVarintContinuationBit) == 0) {
            result |= (byte << shift);
            *value = result;
            return p;
        } else {
            result |= ((byte & kVarintValueMask) << shift);
        }
    }
    return nullptr;
}

bool GeiVarint64(Slice* input, uint64_t* value) {
    const char* start = input->data();
    const char* limit = start + input->size();
    const char* parsed = GetVarint64Ptr(start, limit, value);
    if (parsed == nullptr) {
        return false;
    }
    input->remove_prefix(static_cast<size_t>(parsed - start));
    return true;
}

void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
    assert(value.size() <= std::numeric_limits<uint32_t>::max());

    PutVarint32(dst, static_cast<uint32_t>(value.size()));
    dst->append(value.data(), value.size());
}

bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
    uint32_t length = 0;
    const char* begin = input->data();
    const char* limit = begin + input->size();
    const char* data = GetVarint32Ptr(begin, limit, &length);

    if (data == nullptr || static_cast<size_t>(limit - data) < length) {
        return false;
    }

    *result = Slice(data, length);
    input->remove_prefix(static_cast<size_t>(data - begin) + length);
    return true;
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