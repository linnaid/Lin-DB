// 声明 VersionSet, Version 最小内存版本管理接口
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

#include <lindb/options.h>
#include <lindb/status.h>

#include "Lin-DB/db/dbformat.h"
#include "Lin-DB/db/version_edit.h"

namespace lindb {

namespace log {
    class Writer;
}

class TableCache;
class Iterator;
class WritableFile;

// 某一时刻的 SSTable 元数据快照
class Version {
public:
    explicit Version(const InternalKeyComparator* comparator, TableCache* table_cache = nullptr);
    Version(const Version& other);
    Version& operator=(const Version& other);

    ~Version();

    struct GetStats {
        FileMetaData* seek_file = nullptr;
        int seek_file_level = -1;
    };

    int NumFiles(int level) const;
    const FileMetaData* File(int level, size_t index) const;
    void AddFile(int level, const FileMetaData& file);
    void RemoveFile(int level, uint64_t file_number);
    void SortFiles();
    Status Get(const ReadOptions& options, const LookupKey& key, std::string* value) const;
    Status Get(const ReadOptions& options, const LookupKey& key, std::string* value, GetStats* stats) const;
    // 把当前 version 的所有 SSTable iterator 加入 iters
    void AddIterators(const ReadOptions& options, std::vector<Iterator*>* iters) const;
    // 判断user_key 是否落在文件 smallest/largest 范围内
    bool FileMayContainUserkey(const FileMetaData* file, const Slice& user_key) const;
    // 检测读放大并标记 seek
    bool UpdateStats(const GetStats& stats);
    FileMetaData* FileToCompact() const;
    int FileToCompactLevel() const;

private:
    FileMetaData* FindFileInLevel(int level, const Slice& user_key) const;

    const InternalKeyComparator* comparator_;
    std::vector<FileMetaData*> files_[kNumLevels];
    TableCache* table_cache_;

    // 保存 seek 触发的 compaction 候选文件
    FileMetaData* file_to_compact_ = nullptr;
    int file_to_compact_level_ = -1;
};

// 管理当前 Version，文件号/log/sequence 等版本级元数据
class VersionSet {
public:
    VersionSet(const std::string& dbname, const Options* options, TableCache* table_cache, 
        const InternalKeyComparator* comparator);
    VersionSet(const VersionSet&) = delete;
    VersionSet& operator=(const VersionSet&) = delete;

    ~VersionSet();

    // 把 Version 写入 MANIFEST，并安装成新的当前 Version
    Status LogAndApply(VersionEdit* edit);
    Status Recover();

    // 把 VersionEdit 应用到 current_，生成并安装新 Version
    Status ApplyVersionEdit(VersionEdit* edit);
    Version* current() const;
    uint64_t NewFileNumber();

    // 标记某个文件号已被使用，避免重新分配
    void MarkFileNumberUsed(uint64_t number);
    SequenceNumber LastSequence() const;
    uint64_t LogNumber() const;

private:
    Status CheckComparatorName(const VersionEdit& edit) const;
    void ApplyMetadata(const VersionEdit& edit);

    // 把当前完整 Version 写成 MANIFEST snapshot
    Status WriteSnapshot(log::Writer* writer) const;

    // 保存文件系统环境，供 MANIFEST/CURRENT 读写使用
    Env* env_;
    std::string dbname_;
    const Options* options_;
    TableCache* table_cache_;
    const InternalKeyComparator* comparator_;
    uint64_t next_file_number_;
    uint64_t log_number_;
    SequenceNumber last_sequence_;
    // 当前 MANIFEST 文件号
    uint64_t manifest_file_number_;

    // 当前打开 MANIFEST 文件句柄
    WritableFile* descriptor_file_;
    std::unique_ptr<log::Writer> descriptor_log_;
    Version* current_;
};

}