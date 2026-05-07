#include <string>  // 使用 std::string 保存测试中的 key/value 和编码内容。
#include <vector>  // 使用 std::vector 收集 WriteBatch::Iterate 回放出的记录。

#include <lindb/write_batch.h>  // 使用 WriteBatch 的公开批量写接口。

#include "db/dbformat.h"  // 使用 InternalKeyComparator、LookupKey、SequenceNumber 等内部 key 语义。
#include "db/memtable.h"  // 使用 MemTable 验证 WriteBatch 能回放到内存表。
#include "db/write_batch_internal.h"  // 使用 WriteBatchInternal 测试 count、sequence、InsertInto 等内部能力。
#include "tests/test_util.h"  // 使用项目已有的轻量测试断言宏。

namespace {  // 进入匿名命名空间，避免测试辅助类型污染全局符号。

struct SeenRecord {  // 定义 Iterate 回放时观察到的一条逻辑写记录。
    std::string type;  // 保存记录类型，取值为 put 或 delete。
    std::string key;  // 保存记录中的 user key。
    std::string value;  // 保存 Put 记录中的 value，Delete 记录保持为空字符串。
};  // 结束 SeenRecord 定义。

class CollectingHandler : public lindb::WriteBatch::Handler {  // 定义一个收集 Iterate 回调结果的 Handler。
public:  // 以下是 Handler 接口实现。
    void Put(const lindb::Slice& key, const lindb::Slice& value) override {  // 处理一条 Put 回放记录。
        records.push_back({"put", key.ToString(), value.ToString()});  // 把 Put 的类型、key、value 保存到 records。
    }  // 结束 Put 回调。

    void Delete(const lindb::Slice& key) override {  // 处理一条 Delete 回放记录。
        records.push_back({"delete", key.ToString(), ""});  // 把 Delete 的类型和 key 保存到 records。
    }  // 结束 Delete 回调。

    std::vector<SeenRecord> records;  // 保存所有已经回放出的逻辑写记录。
};  // 结束 CollectingHandler 定义。

}  // namespace

int main() {  // 测试 WriteBatch 最小实现的入口函数。
    {  // 测试空 batch 的默认 header 状态。
        lindb::WriteBatch batch;  // 创建一个空 WriteBatch。
        LINDB_EXPECT_EQ(lindb::WriteBatchInternal::Sequence(&batch), static_cast<lindb::SequenceNumber>(0));  // 验证默认起始 sequence 是 0。
        LINDB_EXPECT_EQ(lindb::WriteBatchInternal::Count(&batch), 0);  // 验证默认 record 数量是 0。
        CollectingHandler handler;  // 创建收集型回放 handler。
        LINDB_EXPECT_TRUE(batch.Iterate(&handler).ok());  // 验证空 batch 可以正常 Iterate。
        LINDB_EXPECT_EQ(handler.records.size(), static_cast<size_t>(0));  // 验证空 batch 不会回放出任何记录。
    }  // 结束空 batch 测试。

    {  // 测试 Put/Delete 编码后能按顺序 Iterate 回放。
        lindb::WriteBatch batch;  // 创建一个新的 WriteBatch。
        batch.Put("k1", "v1");  // 追加一条 Put 记录。
        batch.Delete("k2");  // 追加一条 Delete 记录。
        LINDB_EXPECT_EQ(lindb::WriteBatchInternal::Count(&batch), 2);  // 验证 header 中的 record 数量是 2。

        CollectingHandler handler;  // 创建收集型回放 handler。
        LINDB_EXPECT_TRUE(batch.Iterate(&handler).ok());  // 遍历 batch 并验证解码成功。
        LINDB_EXPECT_EQ(handler.records.size(), static_cast<size_t>(2));  // 验证共回放出 2 条记录。
        LINDB_EXPECT_EQ(handler.records[0].type, std::string("put"));  // 验证第 1 条记录是 Put。
        LINDB_EXPECT_EQ(handler.records[0].key, std::string("k1"));  // 验证第 1 条记录的 key 是 k1。
        LINDB_EXPECT_EQ(handler.records[0].value, std::string("v1"));  // 验证第 1 条记录的 value 是 v1。
        LINDB_EXPECT_EQ(handler.records[1].type, std::string("delete"));  // 验证第 2 条记录是 Delete。
        LINDB_EXPECT_EQ(handler.records[1].key, std::string("k2"));  // 验证第 2 条记录的 key 是 k2。
    }  // 结束 Iterate 回放测试。

    {  // 测试 sequence 写入和 MemTable 回放语义。
        lindb::WriteBatch batch;  // 创建一个新的 WriteBatch。
        batch.Put("old", "v1");  // 第 1 条记录会使用起始 sequence。
        batch.Delete("gone");  // 第 2 条记录会使用起始 sequence + 1。
        batch.Put("new", "v2");  // 第 3 条记录会使用起始 sequence + 2。
        lindb::WriteBatchInternal::SetSequence(&batch, 100);  // 设置 batch 起始 sequence 为 100。
        LINDB_EXPECT_EQ(lindb::WriteBatchInternal::Sequence(&batch), static_cast<lindb::SequenceNumber>(100));  // 验证 sequence 写入成功。

        lindb::InternalKeyComparator comparator(lindb::BytewiseComparator());  // 创建 MemTable 需要的 internal key comparator。
        lindb::MemTable memtable(comparator);  // 创建用于接收回放结果的 MemTable。
        LINDB_EXPECT_TRUE(lindb::WriteBatchInternal::InsertInto(&batch, &memtable).ok());  // 把 batch 回放进 MemTable 并验证成功。

        std::string value;  // 保存 MemTable::Get 查询结果。
        lindb::LookupKey old_key("old", 100);  // 构造 sequence=100 视角下查询 old 的 lookup key。
        LINDB_EXPECT_TRUE(memtable.Get(old_key, &value));  // 验证 old 在 sequence=100 可见。
        LINDB_EXPECT_EQ(value, std::string("v1"));  // 验证 old 的 value 是 v1。

        lindb::LookupKey gone_key("gone", 101);  // 构造 sequence=101 视角下查询 gone 的 lookup key。
        LINDB_EXPECT_FALSE(memtable.Get(gone_key, &value));  // 验证 gone 的最新记录是删除标记，所以不可见。

        lindb::LookupKey new_key("new", 102);  // 构造 sequence=102 视角下查询 new 的 lookup key。
        LINDB_EXPECT_TRUE(memtable.Get(new_key, &value));  // 验证 new 在 sequence=102 可见。
        LINDB_EXPECT_EQ(value, std::string("v2"));  // 验证 new 的 value 是 v2。
    }  // 结束 MemTable 回放测试。

    {  // 测试 Append 会拼接记录并更新 count。
        lindb::WriteBatch first;  // 创建目标 batch。
        first.Put("a", "1");  // 在目标 batch 中写入一条 Put。

        lindb::WriteBatch second;  // 创建源 batch。
        second.Put("b", "2");  // 在源 batch 中写入一条 Put。
        second.Delete("a");  // 在源 batch 中写入一条 Delete。

        first.Append(second);  // 把 second 的记录追加到 first 后面。
        LINDB_EXPECT_EQ(lindb::WriteBatchInternal::Count(&first), 3);  // 验证追加后 count 等于 3。

        CollectingHandler handler;  // 创建收集型回放 handler。
        LINDB_EXPECT_TRUE(first.Iterate(&handler).ok());  // 遍历追加后的 batch 并验证成功。
        LINDB_EXPECT_EQ(handler.records.size(), static_cast<size_t>(3));  // 验证追加后可以回放 3 条记录。
        LINDB_EXPECT_EQ(handler.records[0].key, std::string("a"));  // 验证第 1 条来自 first。
        LINDB_EXPECT_EQ(handler.records[1].key, std::string("b"));  // 验证第 2 条来自 second。
        LINDB_EXPECT_EQ(handler.records[2].type, std::string("delete"));  // 验证第 3 条是 second 中的 Delete。
    }  // 结束 Append 测试。

    {  // 测试 Clear 会清空记录并重置 header。
        lindb::WriteBatch batch;  // 创建一个新的 WriteBatch。
        batch.Put("x", "y");  // 先写入一条记录。
        lindb::WriteBatchInternal::SetSequence(&batch, 7);  // 设置一个非零 sequence。
        batch.Clear();  // 清空 batch。
        LINDB_EXPECT_EQ(lindb::WriteBatchInternal::Sequence(&batch), static_cast<lindb::SequenceNumber>(0));  // 验证 Clear 后 sequence 重置为 0。
        LINDB_EXPECT_EQ(lindb::WriteBatchInternal::Count(&batch), 0);  // 验证 Clear 后 count 重置为 0。
    }  // 结束 Clear 测试。

    {  // 测试残缺编码会返回 Corruption。
        lindb::WriteBatch batch;  // 创建一个新的 WriteBatch。
        batch.Put("broken", "value");  // 写入一条完整 Put 记录。
        std::string contents = lindb::WriteBatchInternal::Contents(&batch).ToString();  // 复制 batch 的完整二进制编码。
        contents.pop_back();  // 删除最后一个字节，模拟残缺的 batch 编码。
        lindb::WriteBatchInternal::SetContents(&batch, lindb::Slice(contents));  // 用残缺编码覆盖 batch 内容。

        CollectingHandler handler;  // 创建收集型回放 handler。
        LINDB_EXPECT_TRUE(batch.Iterate(&handler).IsCorruption());  // 验证残缺编码会被识别为 Corruption。
    }  // 结束 Corruption 测试。

    return 0;  // 所有检查通过，测试进程正常退出。
}  // 结束 main。
