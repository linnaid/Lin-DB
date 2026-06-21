// 定义当前最小 DB 内核
// 把 DB 接口, sequence 分配, WAL文件, WAL writer, MemTable 串起来
#pragma once

#include <string>
#include <memory>

#include <lindb/db.h>

#include "Lin-DB/db/dbformat.h"
#include "Lin-DB/db/memtable.h"
#include "Lin-DB/db/log_writer.h"
#include "Lin-DB/db/table_cache.h"
#include "Lin-DB/db/version_set.h"


namespace lindb {

class WriteBatch;

// 保存一个固定 sequence，作为 ReadOptions::snapshot 的真实实现
class SnapshotImpl final : public Snapshot {
public:
    // 构造 snapshot，记录当前 last sequence
    explicit SnapshotImpl(SequenceNumber sequence);
    SequenceNumber sequence() const;

private:
    SequenceNumber sequence_;
};

class DBImpl final : public DB {
public:
    DBImpl(const Options& options, std::string dbname);
    ~DBImpl() override;

    Status Put(const WriteOptions& options, const Slice& key, const Slice& value) override;
    Status Delete(const WriteOptions& options, const Slice& key) override;
    Status Get(const ReadOptions& options, const Slice& key, std::string* value) override;

    // 创建用户级 iterator，隐藏旧版本和删除标记
    Iterator* NewIterator(const ReadOptions& options) override;
    const Snapshot* GetSnapshot() override;
    void ReleaseSnapshot(const Snapshot* snapshot) override;

    Status Open();

private:
    // 执行统一写入路径：分配 sequence 并回放 batch
    Status Write(const WriteOptions& options, WriteBatch* batch);
    SequenceNumber NewSequence();
    Status InitWAL();

    // 写入前检查 mutable MemTable 是否超过 write_buffer_size，超过就触发同步 flush
    Status MakeRoomForWrite();
    Status FlushMemTable();

    // 从 ReadOptions 里取读视图 sequence
    SequenceNumber GetSnapshotSequence(const ReadOptions& options) const;

    Options options_;
    std::string dbname_;
    InternalKeyComparator internal_comparator_;
    SequenceNumber last_sequence_;

    WritableFile* log_file_ = nullptr;
    // 把 WriteBatch 编码成 WAL physical records
    std::unique_ptr<log::Writer> log_;

    Options table_options_;
    // 缓存 SSTable reader
    TableCache table_cache_;
    // 管理 MANIFEST 和当前 SSTable 元数据
    VersionSet versions_;
    std::unique_ptr<MemTable> mem_;
    std::unique_ptr<MemTable> imm_;
    // 当前 WAL 文件号
    uint64_t logfile_number_;
};

}