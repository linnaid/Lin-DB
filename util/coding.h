// 负责 "怎么编码"
#pragma once

#include <string>
#include <cstdint>

namespace lindb {
    
// 把 uint64_t 按小端序写入固定8字节缓冲区
void EncodeFixed64(char* dst, uint64_t value);

// 向尾部追加固定的64位整数(作用：写入 internal key 的 tag)
void PutFixed64(std::string* dst, uint64_t value);

// 读出(作用：解析tag)
uint64_t DecodeFixed64(const char* ptr);

// 把 uint32_t 编码成 varint，写入缓冲区(LookupKey 或 memtable entry 会用到)
char* EncodeVarint32(char* dst, uint32_t value);

// 解析 varint32(读取长度前缀)
const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* value);

// 追加 varint32 到字符串(构造长度前缀)
void PutVarint32(std::string* dst, uint32_t value);

// 计算 varint 编码长度(预估 buffer 大小，减少分配)
int VarintLength(uint64_t value);

}
