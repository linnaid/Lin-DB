// 定义当前最小 DB 内核
// 把 DB 接口, sequence 分配, WAL文件, WAL writer, MemTable 串起来
#pragma once

#include <string>
#include <memory>
#include <set>
#include <condition_variable>
#include <mutex>

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
    // 测试入口：强制把当前 memtable 刷成 SSTable；
    Status TEST_CompactMemTable();
    // 强制 compact 指定 level 的 key range
    Status TEST_CompactRange(int level, const Slice* begin, const Slice* end);

private:
    struct CompactionState;

    // 执行统一写入路径：分配 sequence 并回放 batch
    Status Write(const WriteOptions& options, WriteBatch* batch);
    SequenceNumber NewSequence();
    Status InitWAL();

    // 读取一个WAL 文件，把其中的WriteBatch 回放进 MemTable，并推进 last_sequence
    Status RecoverLogFile(uint64_t log_number);
    // 保守删除不再需要的旧DB文件
    Status RemoveObsoleteFiles();
    // 写入前检查 mutable MemTable 是否超过 write_buffer_size，超过就触发同步 flush
    Status MakeRoomForWrite();
    Status FlushMemTable();
    // 强制冻结当前 mem_ 或处理已有 imm_，复用 FlushMemTable 完成 flush
    // 把当前 memtable 内容刷盘到 L0 SSTable
    Status CompactMemTable();
    void MaybeScheduleCompaction();
    // Env::Schedule 使用的静态回调入口
    static void BGWork(void* db);
    void BackgroundCall();
    Status BackgroundCompaction();
    Status RunCompaction(Compaction* compaction);
    void RecordBackgroundError(const Status& status);

    // 从 ReadOptions 里取读视图 sequence
    SequenceNumber GetSnapshotSequence(const ReadOptions& options) const;

    // 执行 cmpaction merge 主循环
    Status DoCompactionWork(CompactionState* compact);
    void CleanupCompaction(CompactionState* compact);
    // 创建新的 SSTable 输出文件
    Status OpenCompactionOutputFile(CompactionState* compact);
    Status FinishCOmpactionOutputFile(CompactionState* compact, Iterator* input);
    // 把 compaction 输出安装到 VersionSet
    Status InstallCompactionResults(CompactionState* compact);
    SequenceNumber SmallestSnapshot() const;

    Options options_;
    std::string dbname_;
    InternalKeyComparator internal_comparator_;
    SequenceNumber last_sequence_;
    std::multiset<SequenceNumber> snapshots_;
    // 正在生成但还没安装金 VersionSet 的compaction 输出文件号
    std::set<uint64_t> pending_outputs_;

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

    // 保存后台 flush/compaction 的第一处错误，默认构造即 OK
    Status bg_error_;
    std::mutex background_mu_;
    // 唤醒等待后台任务结束的析构或测试逻辑
    std::condition_variable background_cv_;
    // 标记 DBImpl 正在析构，不再接受新的后台任务
    bool shutting_down_ = false;
    // 标记已有后台 compaction 被排队或正在运行
    bool background_compaction_scheduled_ = false;
    // 标记后台线程当前正在执行 DBImpl::BackgroundCall
    bool background_compaction_running_ = false;
};

}
