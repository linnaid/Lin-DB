// 提供轻量日志和调试字符串工具，后续 filename、VersionEdit、MANIFEST debug、测试输出都会用到
#pragma once

#include <cstdint>
#include <string>

#include <lindb/slice.h>

namespace lindb {

// 把 num 的十进制形式追加到 str
void AppendNumberTo(std::string* str, uint64_t num);

// 把二进制内容转义后追加到 str
void AppendEscapedStringTo(std::string* str, const Slice& value);

// 返回 num 的十进制字符串形式
std::string NumberToString(uint64_t num);

// 把不可打印字符(二进制)转义成可读字符串
std::string EscapeString(const Slice& value);

// 从 input 前缀解析十进制数字并推进 Slice
bool ConsumeDecimalNumber(Slice* input, uint64_t* value);

}