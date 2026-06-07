// 声明 VersionSet, Version 最小内存版本管理接口
// 本文件只放接口，不做 MANIFEST/Get/Recover
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include <lindb/options.h>
#include <lindb/status.h>

#include "Lin-DB/db/dbformat.h"
#include "Lin-DB/db/version_edit.h"

namespace lindb {
class TableCache;

// 某一时刻的 SSTable 元数据快照
class Version {
public:
    explicit Version(const InternalKeyComparator* comparator);
    Version(const Version& other);
    Version& operator=(const Version& other);

    ~Version();

    int NumFiles(int level) const;
    const FileMetaData* File(int level, size_t index) const;
    void AddFile(int level, const FileMetaData& file);
    void RemoveFile(int level, uint64_t file_number);
    void SortFiles();

private:
    const InternalKeyComparator* comparator_;
    std::vector<FileMetaData*> files_[kNumLevels];
};

// 管理当前 Version，文件号/log/sequence 等版本级元数据
class VersionSet {
public:
    VersionSet(const std::string& dbname, const Options* options, TableCache* table_cache, 
        const InternalKeyComparator* comparator);
    VersionSet(const VersionSet&) = delete;
    VersionSet& operator=(const VersionSet&) = delete;

    ~VersionSet();

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

    std::string dbname_;
    const Options* options_;
    TableCache* table_cache_;
    const InternalKeyComparator* comparator_;
    uint64_t next_file_number_;
    uint64_t log_number_;
    SequenceNumber last_sequence_;
    Version* current_;
};

}