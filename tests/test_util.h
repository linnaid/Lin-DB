#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace lindb::test {

template <typename T>
std::string FormatValue(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string FormatValue(const std::string& value) {
    return value;
}

inline std::string FormatValue(const char* value) {
    return value == nullptr ? "<null>" : value;
}

}  // namespace lindb::test

#define LINDB_EXPECT_TRUE(condition)                                             \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::cerr << __FILE__ << ":" << __LINE__                             \
                      << " expected true: " #condition "\n";                     \
            return 1;                                                            \
        }                                                                        \
    } while (false)

#define LINDB_EXPECT_FALSE(condition)                                            \
    do {                                                                         \
        if (condition) {                                                         \
            std::cerr << __FILE__ << ":" << __LINE__                             \
                      << " expected false: " #condition "\n";                    \
            return 1;                                                            \
        }                                                                        \
    } while (false)

#define LINDB_EXPECT_EQ(actual, expected)                                        \
    do {                                                                         \
        const auto& actual_value = (actual);                                     \
        const auto& expected_value = (expected);                                 \
        if (!(actual_value == expected_value)) {                                 \
            std::cerr << __FILE__ << ":" << __LINE__                             \
                      << " expected equality\n"                                  \
                      << "  actual:   "                                          \
                      << ::lindb::test::FormatValue(actual_value) << "\n"        \
                      << "  expected: "                                          \
                      << ::lindb::test::FormatValue(expected_value) << "\n";     \
            return 1;                                                            \
        }                                                                        \
    } while (false)
