#include "util/logging.h"

#include <cctype>
#include <cstdio>
#include <limits>
#include <string>


namespace lindb {

void AppendNumberTo(std::string* str, uint64_t num) {
    str->append(NumberToString(num));
}

std::string NumberToString(uint64_t num) {
    return std::to_string(num);
}

std::string EscapeString(const Slice& value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(value.data()[i]);
        if (byte == '\\n') {
            result.append("\\\n");
        } else if (byte == '\\r'){
            result.append("\\\\r");
        } else if (byte == '\\t') {
            result.append("\\\\t");
        } else if (byte == '\\\\') {
            result.append("\\\\\\\\");
        } else if (std::isprint(byte) != 0) {
            result.push_back(static_cast<char>(byte));
        } else {
            // 保存形如 \\xNN 的转义结果
            char buffer[5];
            std::snprintf(buffer, sizeof(buffer), "\\\\x%02x", byte);
            result.append(buffer);
        }
    }
    return result;
}

bool ConsumDecimalNumber(Slice* input, uint64_t* value) {
    uint64_t result = 0;
    size_t consumed = 0;
    const char* data = input->data();
    const size_t size = input->size();

    while (consumed < size) {
        const unsigned char byte = static_cast<unsigned char>(data[consumed]);
        if (byte < '0' || byte > '9') {
            break;
        }

        const uint64_t digit = static_cast<uint64_t>(byte - '0');
        if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return false;
        }

        result = result * 10 + digit;
        ++consumed;
    }

    if (consumed == 0) {
        return false;
    }

    *value = result;
    input->remove_prefix(consumed);
    return true;
}

}