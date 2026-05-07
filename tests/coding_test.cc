#include <cstdint>  // 使用 uint32_t、uint64_t 校验固定整数和 varint 编码。
#include <limits>  // 使用 numeric_limits 构造 varint 边界值。
#include <string>  // 使用 std::string 保存编码后的二进制字节。
#include <vector>  // 使用 std::vector 保存多组测试值。

#include <lindb/slice.h>  // 使用 Slice 测试 length-prefixed slice 解码。

#include "tests/test_util.h"  // 使用项目已有轻量断言宏。
#include "util/coding.h"  // 使用本步骤要补齐的编码工具函数。

int main() {  // 编码工具层测试入口。
    {  // 测试 fixed32 小端编码和解码。
        char buf[4];  // 准备 4 字节缓冲区保存 fixed32 编码。
        const uint32_t value = 0x11223344U;  // 构造一个每个字节都不同的测试值。
        lindb::EncodeFixed32(buf, value);  // 把 value 按小端序写入 buf。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[0]), static_cast<unsigned char>(0x44));  // 验证最低字节在前。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[1]), static_cast<unsigned char>(0x33));  // 验证第 2 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[2]), static_cast<unsigned char>(0x22));  // 验证第 3 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[3]), static_cast<unsigned char>(0x11));  // 验证最高字节在后。
        LINDB_EXPECT_EQ(lindb::DecodeFixed32(buf), value);  // 验证 fixed32 解码能还原原值。
    }  // 结束 fixed32 测试。

    {  // 测试 PutFixed32 会向字符串追加 4 字节 fixed32。
        std::string dst;  // 保存编码结果。
        lindb::PutFixed32(&dst, 7);  // 追加 fixed32 编码。
        LINDB_EXPECT_EQ(dst.size(), static_cast<size_t>(4));  // fixed32 编码长度必须为 4。
        LINDB_EXPECT_EQ(lindb::DecodeFixed32(dst.data()), static_cast<uint32_t>(7));  // 验证追加内容可被正确解码。
    }  // 结束 PutFixed32 测试。

    {  // 测试 fixed64 小端编码和解码。
        char buf[8];  // 准备 8 字节缓冲区保存 fixed64 编码。
        const uint64_t value = 0x1122334455667788ULL;  // 构造一个每个字节都不同的 64 位测试值。
        lindb::EncodeFixed64(buf, value);  // 把 value 按小端序写入 buf。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[0]), static_cast<unsigned char>(0x88));  // 验证最低字节在前。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[1]), static_cast<unsigned char>(0x77));  // 验证第 2 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[2]), static_cast<unsigned char>(0x66));  // 验证第 3 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[3]), static_cast<unsigned char>(0x55));  // 验证第 4 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[4]), static_cast<unsigned char>(0x44));  // 验证第 5 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[5]), static_cast<unsigned char>(0x33));  // 验证第 6 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[6]), static_cast<unsigned char>(0x22));  // 验证第 7 个字节。
        LINDB_EXPECT_EQ(static_cast<unsigned char>(buf[7]), static_cast<unsigned char>(0x11));  // 验证最高字节在后。
        LINDB_EXPECT_EQ(lindb::DecodeFixed64(buf), value);  // 验证 fixed64 解码能还原原值。
    }  // 结束 fixed64 测试。

    {  // 测试 PutFixed64 会向字符串追加 8 字节 fixed64。
        std::string dst;  // 保存编码结果。
        lindb::PutFixed64(&dst, 7);  // 追加 fixed64 编码。
        LINDB_EXPECT_EQ(dst.size(), static_cast<size_t>(8));  // fixed64 编码长度必须为 8。
        LINDB_EXPECT_EQ(lindb::DecodeFixed64(dst.data()), static_cast<uint64_t>(7));  // 验证追加内容可被正确解码。
    }  // 结束 PutFixed64 测试。

    {  // 测试 varint32 多个边界值的编码和解码。
        const std::vector<uint32_t> values = {0U, 1U, 127U, 128U, 255U, 300U, 16384U, std::numeric_limits<uint32_t>::max()};  // 覆盖单字节、多字节和最大值。
        for (uint32_t value : values) {  // 遍历每个 varint32 测试值。
            char buf[5];  // varint32 最多占 5 字节。
            char* end = lindb::EncodeVarint32(buf, value);  // 编码当前值。
            uint32_t parsed = 0;  // 保存解码结果。
            const char* decoded = lindb::GetVarint32Ptr(buf, end, &parsed);  // 从缓冲区解码 varint32。
            LINDB_EXPECT_TRUE(decoded != nullptr);  // 验证解码成功。
            LINDB_EXPECT_EQ(parsed, value);  // 验证解码值等于原值。
            LINDB_EXPECT_TRUE(decoded == end);  // 验证解码正好消费全部 varint 字节。
        }  // 结束 varint32 边界循环。
    }  // 结束 varint32 编解码测试。

    {  // 测试 PutVarint32 和 GetVarint32 会推进 Slice。
        std::string dst;  // 保存编码结果。
        lindb::PutVarint32(&dst, 16384);  // 写入一个三字节 varint32。
        dst.append("tail", 4);  // 追加尾部内容，验证 Slice 只消费 varint 部分。
        lindb::Slice input(dst);  // 用 Slice 包装完整输入。
        uint32_t parsed = 0;  // 保存解码结果。
        LINDB_EXPECT_TRUE(lindb::GetVarint32(&input, &parsed));  // 解码并推进 Slice。
        LINDB_EXPECT_EQ(parsed, static_cast<uint32_t>(16384));  // 验证 varint32 值正确。
        LINDB_EXPECT_EQ(input.ToString(), std::string("tail"));  // 验证剩余输入仍是 tail。
    }  // 结束 GetVarint32 推进测试。

    {  // 测试 varint64 多个边界值的编码和解码。
        const std::vector<uint64_t> values = {0ULL, 1ULL, 127ULL, 128ULL, 255ULL, 300ULL, 16384ULL, 1ULL << 32, std::numeric_limits<uint64_t>::max()};  // 覆盖 64 位边界。
        for (uint64_t value : values) {  // 遍历每个 varint64 测试值。
            char buf[10];  // varint64 最多占 10 字节。
            char* end = lindb::EncodeVarint64(buf, value);  // 编码当前值。
            uint64_t parsed = 0;  // 保存解码结果。
            const char* decoded = lindb::GetVarint64Ptr(buf, end, &parsed);  // 从缓冲区解码 varint64。
            LINDB_EXPECT_TRUE(decoded != nullptr);  // 验证解码成功。
            LINDB_EXPECT_EQ(parsed, value);  // 验证解码值等于原值。
            LINDB_EXPECT_TRUE(decoded == end);  // 验证解码正好消费全部 varint 字节。
        }  // 结束 varint64 边界循环。
    }  // 结束 varint64 编解码测试。

    {  // 测试 PutVarint64 和 GetVarint64 会推进 Slice。
        std::string dst;  // 保存编码结果。
        lindb::PutVarint64(&dst, 1ULL << 40);  // 写入一个大于 32 位的 varint64。
        dst.append("rest", 4);  // 追加尾部内容，验证 Slice 只消费 varint 部分。
        lindb::Slice input(dst);  // 用 Slice 包装完整输入。
        uint64_t parsed = 0;  // 保存解码结果。
        LINDB_EXPECT_TRUE(lindb::GetVarint64(&input, &parsed));  // 解码并推进 Slice。
        LINDB_EXPECT_EQ(parsed, static_cast<uint64_t>(1ULL << 40));  // 验证 varint64 值正确。
        LINDB_EXPECT_EQ(input.ToString(), std::string("rest"));  // 验证剩余输入仍是 rest。
    }  // 结束 GetVarint64 推进测试。

    {  // 测试 length-prefixed slice 支持普通字符串、空字符串和二进制字符串。
        std::string dst;  // 保存多个 length-prefixed slice 编码。
        const std::string binary("a\0b", 3);  // 构造包含零字节的二进制字符串。
        lindb::PutLengthPrefixedSlice(&dst, lindb::Slice("hello"));  // 编码普通字符串。
        lindb::PutLengthPrefixedSlice(&dst, lindb::Slice(""));  // 编码空字符串。
        lindb::PutLengthPrefixedSlice(&dst, lindb::Slice(binary));  // 编码二进制字符串。
        lindb::Slice input(dst);  // 用 Slice 包装完整输入。
        lindb::Slice result;  // 保存每次解码出的 Slice。
        LINDB_EXPECT_TRUE(lindb::GetLengthPrefixedSlice(&input, &result));  // 解码第一段字符串。
        LINDB_EXPECT_EQ(result.ToString(), std::string("hello"));  // 验证普通字符串正确。
        LINDB_EXPECT_TRUE(lindb::GetLengthPrefixedSlice(&input, &result));  // 解码第二段字符串。
        LINDB_EXPECT_EQ(result.ToString(), std::string(""));  // 验证空字符串正确。
        LINDB_EXPECT_TRUE(lindb::GetLengthPrefixedSlice(&input, &result));  // 解码第三段字符串。
        LINDB_EXPECT_EQ(result.ToString(), binary);  // 验证二进制字符串正确。
        LINDB_EXPECT_TRUE(input.empty());  // 验证所有输入都被消费。
    }  // 结束 length-prefixed slice 正常路径测试。

    {  // 测试残缺 varint 会返回失败。
        std::string bad;  // 保存残缺 varint。
        bad.push_back(static_cast<char>(0x80));  // 写入只有 continuation bit、没有结束字节的 varint。
        lindb::Slice input(bad);  // 用 Slice 包装残缺输入。
        uint32_t parsed = 0;  // 保存解码输出。
        LINDB_EXPECT_FALSE(lindb::GetVarint32(&input, &parsed));  // 残缺 varint32 应该失败。
        LINDB_EXPECT_EQ(input.size(), bad.size());  // 失败时不应推进 Slice。
    }  // 结束残缺 varint 测试。

    {  // 测试超出 varint32 表示范围的编码会被拒绝。
        const std::string overflow("\x81\x82\x83\x84\x85\x11", 6);  // 构造一个超过 32 位范围的坏 varint32。
        uint32_t parsed = 0;  // 保存解码输出。
        LINDB_EXPECT_TRUE(lindb::GetVarint32Ptr(overflow.data(), overflow.data() + overflow.size(), &parsed) == nullptr);  // 超范围编码必须返回失败。
    }  // 结束 varint32 溢出测试。

    {  // 测试 varint32 任意前缀截断都会失败，只有完整输入才能成功。
        std::string encoded;  // 保存一个完整的 varint32 编码。
        const uint32_t value = (1u << 31) + 100u;  // 选择一个需要 5 字节编码的大值。
        lindb::PutVarint32(&encoded, value);  // 生成完整的 varint32 编码。
        uint32_t parsed = 0;  // 保存解码输出。
        for (size_t len = 0; len + 1 < encoded.size(); ++len) {  // 遍历所有不完整前缀长度。
            LINDB_EXPECT_TRUE(lindb::GetVarint32Ptr(encoded.data(), encoded.data() + len, &parsed) == nullptr);  // 任意截断前缀都必须失败。
        }  // 结束所有截断前缀检查。
        LINDB_EXPECT_TRUE(lindb::GetVarint32Ptr(encoded.data(), encoded.data() + encoded.size(), &parsed) != nullptr);  // 完整输入必须能够成功解码。
        LINDB_EXPECT_EQ(parsed, value);  // 验证完整输入解码值正确。
    }  // 结束 varint32 截断测试。

    {  // 测试残缺 varint64 会返回失败且不推进 Slice。
        std::string bad;  // 保存残缺 varint64。
        bad.push_back(static_cast<char>(0x80));  // 写入 continuation bit。
        bad.push_back(static_cast<char>(0x80));  // 再写一个 continuation bit，仍然没有结束字节。
        lindb::Slice input(bad);  // 用 Slice 包装残缺输入。
        uint64_t parsed = 0;  // 保存解码输出。
        LINDB_EXPECT_FALSE(lindb::GetVarint64(&input, &parsed));  // 残缺 varint64 应该失败。
        LINDB_EXPECT_EQ(input.size(), bad.size());  // 失败时不应推进 Slice。
    }  // 结束残缺 varint64 测试。

    {  // 测试超出 varint64 表示范围的编码会被拒绝。
        const std::string overflow("\x81\x82\x83\x84\x85\x81\x82\x83\x84\x85\x11", 11);  // 构造一个超过 64 位范围的坏 varint64。
        uint64_t parsed = 0;  // 保存解码输出。
        LINDB_EXPECT_TRUE(lindb::GetVarint64Ptr(overflow.data(), overflow.data() + overflow.size(), &parsed) == nullptr);  // 超范围编码必须返回失败。
    }  // 结束 varint64 溢出测试。

    {  // 测试 varint64 任意前缀截断都会失败，只有完整输入才能成功。
        std::string encoded;  // 保存一个完整的 varint64 编码。
        const uint64_t value = (1ULL << 63) + 100ULL;  // 选择一个需要 10 字节编码的大值。
        lindb::PutVarint64(&encoded, value);  // 生成完整的 varint64 编码。
        uint64_t parsed = 0;  // 保存解码输出。
        for (size_t len = 0; len + 1 < encoded.size(); ++len) {  // 遍历所有不完整前缀长度。
            LINDB_EXPECT_TRUE(lindb::GetVarint64Ptr(encoded.data(), encoded.data() + len, &parsed) == nullptr);  // 任意截断前缀都必须失败。
        }  // 结束所有截断前缀检查。
        LINDB_EXPECT_TRUE(lindb::GetVarint64Ptr(encoded.data(), encoded.data() + encoded.size(), &parsed) != nullptr);  // 完整输入必须能够成功解码。
        LINDB_EXPECT_EQ(parsed, value);  // 验证完整输入解码值正确。
    }  // 结束 varint64 截断测试。

    {  // 测试残缺 length-prefixed payload 会返回失败。
        std::string bad;  // 保存残缺 length-prefixed slice。
        lindb::PutVarint32(&bad, 5);  // 声明 payload 长度为 5。
        bad.append("abc", 3);  // 实际只写入 3 字节 payload。
        lindb::Slice input(bad);  // 用 Slice 包装残缺输入。
        lindb::Slice result;  // 保存解码输出。
        LINDB_EXPECT_FALSE(lindb::GetLengthPrefixedSlice(&input, &result));  // payload 不足时应该失败。
        LINDB_EXPECT_EQ(input.size(), bad.size());  // 失败时不应推进 Slice。
    }  // 结束残缺 payload 测试。

    LINDB_EXPECT_EQ(lindb::VarintLength(0), 1);  // 验证 0 的 varint 长度为 1。
    LINDB_EXPECT_EQ(lindb::VarintLength(1), 1);  // 验证 1 的 varint 长度为 1。
    LINDB_EXPECT_EQ(lindb::VarintLength(127), 1);  // 验证 127 的 varint 长度为 1。
    LINDB_EXPECT_EQ(lindb::VarintLength(128), 2);  // 验证 128 的 varint 长度为 2。
    LINDB_EXPECT_EQ(lindb::VarintLength(16384), 3);  // 验证 16384 的 varint 长度为 3。
    LINDB_EXPECT_EQ(lindb::VarintLength(std::numeric_limits<uint64_t>::max()), 10);  // 验证 uint64 最大值的 varint 长度为 10。

    return 0;  // 所有编码测试通过。
}  // 结束 main。
