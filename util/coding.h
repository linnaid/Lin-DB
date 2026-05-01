// 负责 "怎么编码"
#pragma once

#include <string>
#include <cstdint>

#include <lindb/slice.h>

namespace lindb {

// 把 uint32_t 按小端序写入固定4字节缓冲区
void EncodeFixed32(char* dst, uint32_t value);

// 向尾部追加固定的32位整数
void PutFixed32(std::string* dst, uint32_t value);

// 从 8 字节小端序缓冲区读出 uint32_t
uint32_t DecodeFixed32(const char* ptr);

    
// 把 uint64_t 按小端序写入固定8字节缓冲区
void EncodeFixed64(char* dst, uint64_t value);

// 向尾部追加固定的64位整数(作用：写入 internal key 的 tag)
void PutFixed64(std::string* dst, uint64_t value);

// 读出(作用：解析tag)
uint64_t DecodeFixed64(const char* ptr);


// 把 uint32_t 编码成 varint，写入缓冲区(LookupKey 或 memtable entry 会用到)
// 返回写入后的尾指针
char* EncodeVarint32(char* dst, uint32_t value);

// 解析 varint32(读取长度前缀)
const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* value);

// 从 Slice 开头解析 varint32，成功后推进 Slice
bool GetVarint32(Slice* input, uint32_t* value);

// 追加 varint32 到字符串(构造长度前缀)
void PutVarint32(std::string* dst, uint32_t value);


// 把 uint64_t 编码成 varint64，写入缓冲区
// 返回写入后的尾指针
char* EncodeVarint64(char* dst, uint64_t value);

// 解析 varint64(读取长度前缀)
const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value);

// 从 Slice 开头解析 varint64，成功后推进 Slice
bool GetVarint64(Slice* input, uint64_t* value);

// 追加 varint64 到字符串(构造长度前缀)
void PutVarint64(std::string* dst, uint64_t value);


// 追加 varint32 长度前缀 + Slice 原始字节
void PutLengthPrefixedSlice(std::string* dst, const Slice& value);

// 从 Slice 开头解析 length-prefixed slice，成功后推进 Slice
bool GetLengthPrefixedSlice(Slice* input, Slice* result);

// 计算 varint 编码长度(预估 buffer 大小，减少分配)
int VarintLength(uint64_t value);

}
