// 定义 DB 打开、读取、写入时用到的配置对象
#pragma once

#include <cstddef>

#include <lindb/comparator.h>

namespace lindb {
    
// 定义打开 DB 时使用的配置集合
struct Options {
    const Comparator* comparator = BytewiseComparator();
    size_t write_buffer_size = 4 * 1024 * 1024;
};

// 定义读操作配置，暂时为空
struct ReadOptions {

};

struct WriteOptions {

};

}