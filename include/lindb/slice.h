#pragma once

#include <cstddef>
#include <string>

namespace lindb {
    
    // 定义一个不拥有内存的只读字节视图
    // 作用：统一表示 key/value/header 等二进制数据，避免到处拷贝 std::string
    class Slice {
    public:
        // 构造函数，构造一个空 SLice
        Slice();
        
        // 指向一段 [data, data+size] 的外部内存
        // 作用：让调用方可以把任意字节数组包装成 Slice
        Slice(const char* data, size_t size);

        // 指向一段 std::string 的内部缓冲区
        // 作用：方便把现有字符串作为只读二进制传递
        Slice(const std::string& str);

        // 指向一段以 '\\0' 结尾的字符串
        Slice(const char* str);

        // 返回数据起始地址
        const char* data() const;

        // 返回字节的长度
        size_t size() const;

        // 判断是否为空
        bool empty() const;

        // 清空视图，不释放外部内存
        void clear();

        // 去掉前 n 个字符
        void remove_prefix(size_t n);

        // 转成 std::string 副本
        std::string ToString() const;

        // 按字典序比较
        int compare(const Slice& other) const;

    private:
        const char* data_;
        size_t size_;
    };
}
