// 定义 WAL 物理 record 格式常量和类型
#pragma once

namespace lindb {
namespace log {

enum RecordType {
    kZeroType = 0,
    kFullType = 1,
    kFirstType = 2,
    kMiddleType = 3,
    kLastType = 4,
};

constexpr int kMaxRecordType = kLastType;
constexpr int kBlockSize = 32768;
constexpr int kHeaderSize = 4 + 2 + 1;

}

}