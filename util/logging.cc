#include "Lin-DB/util/logging.h"

#include <cctype>
#include <cstdio>
#include <limits>
#include <string>


namespace lindb {
namespace {

void AppendHexEscape(std::string* dst, unsigned char byte) {
    char buffer[5];
    std::snprintf(buffer, sizeof(buffer), "\\x%02x", static_cast<unsigned int>(byte));
    dst->append(buffer);
}

}

void AppendEscapedStringTo(std::string* str, const Slice& value) {
    for (size_t i = 0; i < value.size(); i++) {
        const unsigned char byte = static_cast<unsigned char>(value.data()[i]);
        switch (byte)
        {
        case '\n':
            str->append("\\n");
            break;
        case '\r':
            str->append("\\r");
            break;
        case '\t':
            str->append("\\t");
            break;
        case '\\':
            str->append("\\\\");
            break;
        default:
            if (std::isprint(byte) != 0) {
                str->push_back(static_cast<char>(byte));
            } else {
                AppendHexEscape(str, byte);
            }
            break;
        }
    }
}

void AppendNumberTo(std::string* str, uint64_t num) {
    str->append(NumberToString(num));
}

std::string NumberToString(uint64_t num) {
    return std::to_string(num);
}

std::string EscapeString(const Slice& value) {
    std::string result;
    AppendEscapedStringTo(&result, value);
    return result;
}

bool ConsumeDecimalNumber(Slice* input, uint64_t* value) {
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