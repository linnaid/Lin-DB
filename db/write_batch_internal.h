// 定义 DB 内部使用的 WriteBatch 工具接口，外部用户不需要知道 batch 的 sequence/count/rep_
#pragma once

#include <cstddef>

#include <lindb/slice.h>
#include <lindb/status.h>
#include <lindb/write_batch.h>

#include "Lin-DB/db/dbformat.h"

namespace lindb {

// 前向声明
class MemTable;

// 内部 writebatch 操作集合
class WriteBatchInternal {
// 以下静态函数供 DBImpl、测试、未来 WAL/Recovery 使用
public:
    // 读取 batch header 中的起始 sequence
    static SequenceNumber Sequence(const WriteBatch* batch);

    // 写入 sequence
    static void SetSequence(WriteBatch* batch, SequenceNumber seq);

    // 获取 batch 中 record 的数量
    static int Count(const WriteBatch* batch);

    // 写入 record 数量
    static void SetCount(WriteBatch* batch, int count);

    // 返回完整 batch 编码内容
    static Slice Contents(const WriteBatch* batch);

    // 返回完整 batch 编码长度
    static size_t ByteSize(const WriteBatch* batch);

    // 重建 batch
    static void SetContents(WriteBatch* batch, const Slice& contents);

    // 把 batch 回放进 Memtable
    static Status InsertInto(const WriteBatch* batch, MemTable* memtable);

    // 把 src 的 records 追加到 dst，更新 header
    static void Append(WriteBatch* dst, const WriteBatch* src);
};

}