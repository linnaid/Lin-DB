// 定义当前最小 DB 内核
// 把 DB 接口, sequence 分配, InternalKeyComparator, MemTable 串起来
#pragma once

#include <string>

#include <lindb/db.h>

#include "Lin-DB/db/dbformat.h"
#include "Lin-DB/db/memtable.h"


namespace lindb {

class DBImpl final : public DB {
public:
    DBImpl(const Options& options, std::string dbname);
    ~DBImpl() override = default;

    Status Put(const WriteOptions& options, const Slice& key, const Slice& value) override;
    Status Delete(const WriteOptions& options, const Slice& key) override;
    Status Get(const ReadOptions& options, const Slice& key, std::string* value) override;

private:
    SequenceNumber NewSequence();

    Options options_;
    std::string dbname_;
    InternalKeyComparator internal_comparator_;
    MemTable mem_;
    SequenceNumber last_sequence_;

};

}