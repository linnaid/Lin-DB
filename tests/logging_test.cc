#include <cstdint>  // 使用 uint64_t 测试数字解析。
#include <limits>  // 使用 numeric_limits 构造溢出场景。
#include <string>  // 使用 std::string 构造和校验输出。

#include <lindb/slice.h>  // 使用 Slice 测试 ConsumeDecimalNumber。

#include "tests/test_util.h"  // 使用项目已有轻量断言宏。
#include "util/logging.h"  // 使用本步骤要新增的 logging 工具。

int main() {  // Logging 工具测试入口。
    {  // 测试 AppendNumberTo 会把数字追加成十进制字符串。
        std::string output = "file-";  // 准备已有前缀。
        lindb::AppendNumberTo(&output, 12345);  // 追加十进制数字。
        LINDB_EXPECT_EQ(output, std::string("file-12345"));  // 验证追加结果。
    }  // 结束 AppendNumberTo 测试。

    {  // 测试 NumberToString 返回十进制字符串。
        LINDB_EXPECT_EQ(lindb::NumberToString(0), std::string("0"));  // 验证 0 的字符串形式。
        LINDB_EXPECT_EQ(lindb::NumberToString(9876543210ULL), std::string("9876543210"));  // 验证大数字字符串形式。
    }  // 结束 NumberToString 测试。

    {  // 测试 NumberToString 在 uint64_t 上界附近仍能稳定输出完整十进制字符串。
        LINDB_EXPECT_EQ(lindb::NumberToString(18446744073709551000ULL), std::string("18446744073709551000"));  // 验证接近上界的大数不会丢位。
        LINDB_EXPECT_EQ(lindb::NumberToString(18446744073709551614ULL), std::string("18446744073709551614"));  // 验证 max-1 的字符串形式正确。
        LINDB_EXPECT_EQ(lindb::NumberToString(std::numeric_limits<uint64_t>::max()), std::string("18446744073709551615"));  // 验证 uint64 最大值的字符串形式正确。
    }  // 结束上界 NumberToString 测试。

    {  // 测试 EscapeString 会转义不可打印字符和反斜杠。
        const std::string input("a\n\\\x01", 4);  // 构造包含换行、反斜杠和 0x01 的输入。
        const std::string escaped = lindb::EscapeString(lindb::Slice(input));  // 转义输入字符串。
        LINDB_EXPECT_EQ(escaped, std::string("a\\n\\\\\\x01"));  // 验证转义结果稳定可读。
    }  // 结束 EscapeString 测试。

    {  // 测试 AppendEscapedStringTo 能把转义结果追加到已有前缀后面。
        std::string output = "key=";  // 准备已有前缀。
        const std::string input("x\t\0", 3);  // 构造包含制表符和零字节的输入。
        lindb::AppendEscapedStringTo(&output, lindb::Slice(input));  // 把转义结果追加到输出。
        LINDB_EXPECT_EQ(output, std::string("key=x\\t\\x00"));  // 验证追加结果稳定。
    }  // 结束 AppendEscapedStringTo 测试。

    {  // 测试 ConsumeDecimalNumber 能解析前缀数字并推进 Slice。
        lindb::Slice input("12345abc");  // 构造数字前缀加尾部字符。
        uint64_t value = 0;  // 保存解析出的数字。
        LINDB_EXPECT_TRUE(lindb::ConsumeDecimalNumber(&input, &value));  // 解析十进制数字。
        LINDB_EXPECT_EQ(value, static_cast<uint64_t>(12345));  // 验证数字值正确。
        LINDB_EXPECT_EQ(input.ToString(), std::string("abc"));  // 验证 Slice 被推进到非数字尾部。
    }  // 结束正常数字解析测试。

    {  // 测试 ConsumeDecimalNumber 在 uint64_t 上界附近也能 round-trip 成功。
        for (uint64_t i = 0; i < 8; ++i) {  // 覆盖 max 到 max-7 这组最容易出错的边界值。
            const uint64_t expected = std::numeric_limits<uint64_t>::max() - i;  // 构造靠近上界的合法输入。
            const std::string encoded = lindb::NumberToString(expected) + "tail";  // 拼接尾部内容，验证只消费数字前缀。
            lindb::Slice input(encoded);  // 用 Slice 包装完整输入。
            uint64_t value = 0;  // 保存解析出的数字。
            LINDB_EXPECT_TRUE(lindb::ConsumeDecimalNumber(&input, &value));  // 靠近上界的合法数字必须能成功解析。
            LINDB_EXPECT_EQ(value, expected);  // 验证 round-trip 后仍等于原值。
            LINDB_EXPECT_EQ(input.ToString(), std::string("tail"));  // 验证解析后只剩下尾部 padding。
        }  // 结束上界 round-trip 循环。
    }  // 结束上界 ConsumeDecimalNumber 测试。

    {  // 测试 ConsumeDecimalNumber 在没有数字时失败且不推进 Slice。
        lindb::Slice input("abc");  // 构造没有数字前缀的输入。
        uint64_t value = 0;  // 保存解析出的数字。
        LINDB_EXPECT_FALSE(lindb::ConsumeDecimalNumber(&input, &value));  // 没有数字应返回失败。
        LINDB_EXPECT_EQ(input.ToString(), std::string("abc"));  // 验证失败时不推进 Slice。
    }  // 结束无数字失败测试。

    {  // 测试 ConsumeDecimalNumber 在多种“非数字开头”输入上都失败且不推进 Slice。
        const std::string cases[] = {  // 构造多组非数字起始输入。
            "",  // 空串不应被解析成数字。
            " 123",  // 前导空格不属于合法数字前缀。
            "a123",  // 前导字母不属于合法数字前缀。
            std::string("\0""123", 4),  // 前导零字节不应被当作数字。
            std::string("\x7f""123", 4),  // 前导 DEL 控制字符不应被当作数字。
            std::string("\xff""123", 4),  // 前导高位字节不应被当作数字。
        };  // 结束坏输入集合定义。
        for (const std::string& text : cases) {  // 遍历每个坏输入样例。
            lindb::Slice input(text);  // 用 Slice 包装当前输入。
            uint64_t value = 0;  // 保存解析出的数字。
            LINDB_EXPECT_FALSE(lindb::ConsumeDecimalNumber(&input, &value));  // 非数字开头必须返回失败。
            LINDB_EXPECT_EQ(input.ToString(), text);  // 失败时不允许推进 Slice。
        }  // 结束坏输入循环。
    }  // 结束多样化无数字失败测试。

    {  // 测试 ConsumeDecimalNumber 溢出时失败。
        std::string too_big = std::to_string(std::numeric_limits<uint64_t>::max());  // 构造 uint64 最大值字符串。
        too_big.push_back('0');  // 追加一位数字制造溢出。
        lindb::Slice input(too_big);  // 用 Slice 包装溢出输入。
        uint64_t value = 0;  // 保存解析出的数字。
        LINDB_EXPECT_FALSE(lindb::ConsumeDecimalNumber(&input, &value));  // 溢出应返回失败。
        LINDB_EXPECT_EQ(input.ToString(), too_big);  // 溢出失败时不应推进 Slice。
    }  // 结束溢出失败测试。

    return 0;  // 所有 Logging 测试通过。
}  // 结束 main。
