// 定义 MANIFEST record 的内存结构和公开接口
// VersionEdit 类声明 + FileMetaData 辅助结构体，负责描述 MANIFEST 中的一次版本元数据变更
#pragma once

#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

#include <lindb/status.h>
#include <lindb/slice.h>

#include "Lin-DB/db/dbformat.h"

namespace lindb {

// class VersionSet;
constexpr int kNumLevels = 7;

// 描述一个 SSTable 文件的元数据
struct FileMetaData {
    int refs = 0;
    uint64_t number = 0;
    uint64_t file_size = 0;
    InternalKey smallest;
    InternalKey largest;
};

// 一次版本元数据变更，可编码进MANIFEST
class VersionEdit {
public:
    using DeletedFileSet = std::set<std::pair<int, uint64_t>>;
    using NewFileSet = std::vector<std::pair<int, FileMetaData>>;

    VersionEdit();

    void Clear();

    void SetComparatorName(const Slice& name);
    void SetLogNumber(uint64_t number);
    void SetNextFile(uint64_t number);
    void SetLastSequence(SequenceNumber sequence);

    void AddFile(int level, uint64_t file_number, uint64_t file_size, 
                 const InternalKey& smallest, const InternalKey& largest);
    void RemoveFile(int level, uint64_t file_number);

    void EncodeTo(std::string* dst) const;
    Status DecodeFrom(const Slice& src);

    bool has_comparator() const { return has_comparator_; }
    bool has_log_number() const { return has_log_number_; }
    bool has_next_file_number() const { return has_next_file_number_; }
    bool has_last_sequence() const { return has_last_sequence_; }

    const std::string& comparator_name() const { return comparator_; }
    uint64_t log_number() const { return log_number_; }
    uint64_t next_file_number() const { return next_file_number_; }
    SequenceNumber last_sequence() const { return last_sequence_; }

    const DeletedFileSet& deleted_files() const { return deleted_files_; }
    const NewFileSet& new_files() const { return new_files_; }

private:
    friend class VersionSet;

    std::string comparator_;
    uint64_t log_number_ = 0;
    uint64_t next_file_number_ = 0;
    SequenceNumber last_sequence_ = 0;
    bool has_comparator_ = false;
    bool has_log_number_ = false;
    bool has_next_file_number_ = false;
    bool has_last_sequence_ = false;
    DeletedFileSet deleted_files_;
    NewFileSet new_files_;
};

}