// 定义用户能看到的 DB 抽象接口
// 外部只通过 DB API 使用数据库，不直接依赖 DBImpl
#pragma once

#include <string>

#include <lindb/options.h>
#include <lindb/slice.h>
#include <lindb/status.h>

namespace lindb {

class Iterator;

// 表示一个固定 sequence 的一致性读视图句柄
class Snapshot {
public:
    Snapshot() = default;
    virtual ~Snapshot() = default;

    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
};

class DB {
public:
    static Status Open(const Options& options, const std::string& dbname, DB** dbptr);

    DB() = default;
    virtual ~DB() = default;

    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;

    virtual Status Put(const WriteOptions& options, const Slice& key, const Slice& value) = 0;
    virtual Status Delete(const WriteOptions& options, const Slice& key) = 0;
    virtual Status Get(const ReadOptions& options, const Slice& key, std::string* value) = 0;

    virtual Iterator* NewIterator(const ReadOptions& options) = 0;
    virtual const Snapshot* GetSnapshot() = 0;
    virtual void ReleaseSnapshot(const Snapshot* snapshot) = 0;
};

}