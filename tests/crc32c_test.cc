#include <cstdint>  // 使用 uint32_t 保存 CRC32C 校验值。
#include <string>  // 使用 std::string 构造测试输入。

#include "tests/test_util.h"  // 使用项目已有轻量断言宏。
#include "util/crc32c.h"  // 使用本步骤要新增的 CRC32C 工具。

int main() {  // CRC32C 工具测试入口。
    {  // 测试空字符串和标准字符串的 CRC32C 值。
        LINDB_EXPECT_EQ(lindb::crc32c::Value("", 0), static_cast<uint32_t>(0x00000000));  // 空输入 CRC32C 应为 0。
        const std::string input = "123456789";  // CRC 算法常用标准测试输入。
        LINDB_EXPECT_EQ(lindb::crc32c::Value(input.data(), input.size()), static_cast<uint32_t>(0xe3069283));  // 验证 Castagnoli CRC32C 标准结果。
    }  // 结束标准值测试。

    {  // 测试 Extend 与一次性 Value 结果一致。
        const std::string left = "hello";  // 第一段输入。
        const std::string right = " world";  // 第二段输入。
        const std::string whole = left + right;  // 拼接后的完整输入。
        uint32_t crc = lindb::crc32c::Value(left.data(), left.size());  // 先计算第一段 CRC。
        crc = lindb::crc32c::Extend(crc, right.data(), right.size());  // 再扩展第二段 CRC。
        LINDB_EXPECT_EQ(crc, lindb::crc32c::Value(whole.data(), whole.size()));  // 验证分段计算等于一次性计算。
    }  // 结束 Extend 测试。

    {  // 测试 Mask 和 Unmask 可逆，并且 Mask 会改变原始 CRC。
        const std::string input = "mask me";  // 构造用于 mask 的输入。
        const uint32_t crc = lindb::crc32c::Value(input.data(), input.size());  // 计算原始 CRC。
        const uint32_t masked = lindb::crc32c::Mask(crc);  // 对 CRC 做 LevelDB 风格 mask。
        LINDB_EXPECT_TRUE(masked != crc);  // 验证 masked 值不同于原值，避免嵌入自身时出现问题。
        LINDB_EXPECT_EQ(lindb::crc32c::Unmask(masked), crc);  // 验证 Unmask 能还原原始 CRC。
    }  // 结束 Mask/Unmask 测试。

    {  // 测试 Mask/Unmask 常量公式与 LevelDB 约定一致。
        LINDB_EXPECT_EQ(lindb::crc32c::Mask(0), static_cast<uint32_t>(0xa282ead8));  // 0 的 masked 结果应等于固定偏移量。
        LINDB_EXPECT_EQ(lindb::crc32c::Unmask(static_cast<uint32_t>(0xa282ead8)), static_cast<uint32_t>(0));  // 固定偏移量反解应回到 0。
    }  // 结束 Mask 公式测试。

    return 0;  // 所有 CRC32C 测试通过。
}  // 结束 main。
