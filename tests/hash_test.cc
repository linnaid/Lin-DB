#include <cstdint>  // 使用 uint32_t 保存 hash 结果。
#include <string>  // 使用 std::string 构造测试输入。

#include "tests/test_util.h"  // 使用项目已有轻量断言宏。
#include "util/hash.h"  // 使用本步骤要新增的 Hash 工具。

int main() {  // Hash 工具测试入口。
    {  // 测试 hash 对同一输入和 seed 保持稳定。
        const std::string input = "hello";  // 构造测试输入。
        const uint32_t first = lindb::Hash(input.data(), input.size(), 0xbc9f1d34);  // 第一次计算 hash。
        const uint32_t second = lindb::Hash(input.data(), input.size(), 0xbc9f1d34);  // 第二次计算相同 hash。
        LINDB_EXPECT_EQ(first, second);  // 验证 hash 稳定可重复。
    }  // 结束稳定性测试。

    {  // 测试不同 seed 会影响 hash 输出。
        const std::string input = "hello";  // 构造测试输入。
        const uint32_t first = lindb::Hash(input.data(), input.size(), 1);  // 使用 seed 1 计算 hash。
        const uint32_t second = lindb::Hash(input.data(), input.size(), 2);  // 使用 seed 2 计算 hash。
        LINDB_EXPECT_TRUE(first != second);  // 验证 seed 参与 hash 计算。
    }  // 结束 seed 测试。

    {  // 测试 hash 支持包含零字节的二进制输入。
        const std::string binary("a\0b", 3);  // 构造包含零字节的输入。
        const uint32_t value = lindb::Hash(binary.data(), binary.size(), 123);  // 计算二进制输入 hash。
        LINDB_EXPECT_EQ(value, lindb::Hash(binary.data(), binary.size(), 123));  // 验证二进制输入 hash 稳定。
    }  // 结束二进制输入测试。

    {  // 测试空输入也能产生确定性 hash。
        const uint32_t value = lindb::Hash("", 0, 99);  // 计算空输入 hash。
        LINDB_EXPECT_EQ(value, lindb::Hash("", 0, 99));  // 验证空输入 hash 稳定。
    }  // 结束空输入测试。

    {  // 测试一组已知输入会产生稳定的 LevelDB 风格 hash 值。
        const uint8_t data1[1] = {0x62};  // 单字节输入。
        const uint8_t data2[2] = {0xc3, 0x97};  // 包含高位字节的两字节输入。
        const uint8_t data3[3] = {0xe2, 0x99, 0xa5};  // 三字节 UTF-8 样式输入。
        const uint8_t data4[4] = {0xe1, 0x80, 0xb9, 0x32};  // 四字节输入。
        const uint8_t data5[48] = {  // 更长的二进制输入，覆盖多轮 4 字节混合。
            0x01, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
            0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x18,
            0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        LINDB_EXPECT_EQ(lindb::Hash(nullptr, 0, 0xbc9f1d34U), static_cast<uint32_t>(0xbc9f1d34U));  // 空指针 + 空长度应直接返回 seed。
        LINDB_EXPECT_EQ(lindb::Hash(reinterpret_cast<const char*>(data1), sizeof(data1), 0xbc9f1d34U), static_cast<uint32_t>(0xef1345c4U));  // 锁定 1 字节输入输出。
        LINDB_EXPECT_EQ(lindb::Hash(reinterpret_cast<const char*>(data2), sizeof(data2), 0xbc9f1d34U), static_cast<uint32_t>(0x5b663814U));  // 锁定 2 字节输入输出。
        LINDB_EXPECT_EQ(lindb::Hash(reinterpret_cast<const char*>(data3), sizeof(data3), 0xbc9f1d34U), static_cast<uint32_t>(0x323c078fU));  // 锁定 3 字节输入输出。
        LINDB_EXPECT_EQ(lindb::Hash(reinterpret_cast<const char*>(data4), sizeof(data4), 0xbc9f1d34U), static_cast<uint32_t>(0xed21633aU));  // 锁定 4 字节输入输出。
        LINDB_EXPECT_EQ(lindb::Hash(reinterpret_cast<const char*>(data5), sizeof(data5), 0x12345678U), static_cast<uint32_t>(0xf333dabbU));  // 锁定长二进制输入输出。
    }  // 结束已知值测试。

    {  // 测试包含零字节的输入不会被当成 C 字符串提前截断。
        const std::string binary("a\0b", 3);  // 构造包含零字节的输入。
        LINDB_EXPECT_TRUE(lindb::Hash(binary.data(), binary.size(), 123) != lindb::Hash("a", 1, 123));  // 若把 \0 当终止符，两个结果会错误地相同。
    }  // 结束零字节不截断测试。

    return 0;  // 所有 Hash 测试通过。
}  // 结束 main。
