// 定义当前最小 DB 内核
// 把 DB 接口, sequence 分配, WAL文件, WAL writer, MemTable 串起来
#pragma once

#include <string>
#include <memory>

#include <lindb/db.h>

#include "Lin-DB/db/dbformat.h"
#include "Lin-DB/db/memtable.h"
#include "Lin-DB/db/log_writer.h"


namespace lindb {

class WriteBatch;

class DBImpl final : public DB {
public:
    DBImpl(const Options& options, std::string dbname);
    ~DBImpl() override;

    Status Put(const WriteOptions& options, const Slice& key, const Slice& value) override;
    Status Delete(const WriteOptions& options, const Slice& key) override;
    Status Get(const ReadOptions& options, const Slice& key, std::string* value) override;

    Status Open();

private:
    // 执行统一写入路径：分配 sequence 并回放 batch
    Status Write(const WriteOptions& options, WriteBatch* batch);
    SequenceNumber NewSequence();
    Status InitWAL();

    Options options_;
    std::string dbname_;
    InternalKeyComparator internal_comparator_;
    MemTable mem_;
    SequenceNumber last_sequence_;

    WritableFile* log_file_ = nullptr;
    // 把 WriteBatch 编码成 WAL physical records
    std::unique_ptr<log::Writer> log_;
};

}