// 提供 Castagnoli CRC32c 校验(定义 CRC32c 工具接口)
// 后续 WAL record & SSTable block trailer 都会用它检测损坏数据
#pragma once

#include <cstddef>
#include <cstdint>

namespace lindb {
// 进入 crc32c 子命名空间，避免与其他 hash/crc 工具混淆
namespace crc32c {
// 计算一段连续字节的 CRC32c
uint32_t Value(const char* data, size_t n);

// 从已有 CRC32c 继续扩展一段字节
uint32_t Extend(uint32_t init_crc, const char* data, size_t n);

// 对 CRC 做 LevelDB 风格 mask，避免 CRC 嵌入自身时出现问题
uint32_t Mask(uint32_t crc);

// 把 Mask 后的 CRC 还原成原始 CRC
uint32_t Unmask(uint32_t masked_crc);
}

}