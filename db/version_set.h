// 声明 VersionSet, Version 最小内存版本管理接口
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <set>

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
class Version;
class VersionSet;

// Compaction Picker(压缩选择器):选择哪些文件将会被合并
// 描述一次待执行 compaction 的元数据计划，当前只负责保存 picker 选出的输入文件
class Compaction {
public:
    ~Compaction();

    // 起始层级
    int level() const;
    // 返回inputs[1/0] 的文件数
    int num_input_files(int which) const;
    const FileMetaData* input(int which, int index) const;
    int num_grandparents() const;
    const FileMetaData* grandparent(int index) const;

    VersionEdit* edit();
    // 单个输出文件最大大小
    uint64_t MaxOutputFileSize() const;
    // 判断本次 compaction 是否可以只移动文件元数据而不重写 SSTable
    // 是否可以直接移动文件到下一层
    bool IsTrivialMove() const;
    // 释放 input_version 引用
    void ReleaseInputs();
    // 判断是否该切换新的输出文件
    bool ShouldStopBefore(const Slice& internal_key);
    // 判断 key 在最高层是否还可见
    bool IsBaseLevelForKey(const Slice& user_key);
    // 把本次 compaction 的所有输入文件标记为删除
    void AddInputDeletions(VersionEdit* edit);

private:
    friend class VersionSet;

    Compaction(const Options* options, int level);

    int level_;
    uint64_t max_output_file_size_;
    Version* input_version_;
    VersionEdit edit_;

    std::vector<FileMetaData*> inputs_[2];
    std::vector<FileMetaData*> grandparents_;
    size_t grandparent_index_;
    // 是否已经输出过至少一个 key
    bool seen_key_;
    int64_t overlapped_bytes_;
    size_t level_ptrs_[kNumLevels];
};


// 修改点3
// Version 中没有prev_ & next_，无法构成循环双向链表
// VersionSet 中没有哨兵节点 dummy_versions_用来维护循环双向链表

// 某一时刻的 SSTable 元数据快照
class Version {
public:
    explicit Version(VersionSet* vset, const InternalKeyComparator* comparator, TableCache* table_cache = nullptr);
    Version(const Version&) = delete;
    Version& operator=(const Version&) = delete;

    ~Version();

    void Ref();
    void Unref();

    struct GetStats {
        FileMetaData* seek_file = nullptr;
        int seek_file_level = -1;
    };

    int NumFiles(int level) const;
    const FileMetaData* File(int level, size_t index) const;
    void AddFile(int level, FileMetaData* file);
    void RemoveFile(int level, uint64_t file_number);
    void SortFiles();

    // 找出该层中与[begin, end] 的 user key 有范围重叠的文件
    void GetOverlappingInputs(int level, const InternalKey* begin,
                              const InternalKey* end, std::vector<FileMetaData*>* inputs) const;

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
    // 返回最高 compaction score
    double CompactionScore() const;
    // 最高 score 所在 level
    int CompactionLevel() const;

private:
    friend class VersionSet;
    friend class Compaction;

    FileMetaData* FindFileInLevel(int level, const Slice& user_key) const;

    // 所属 VersionSet，用于 Unref 时访问 dummy_versions_
    VersionSet* vset_;
    const InternalKeyComparator* comparator_;
    std::vector<FileMetaData*> files_[kNumLevels];
    TableCache* table_cache_;

    int refs_ = 0;
    Version* prev_ = nullptr;
    Version* next_ = nullptr;

    // 保存 seek 触发的 compaction 候选文件
    FileMetaData* file_to_compact_ = nullptr;
    int file_to_compact_level_ = -1;
    // 当前 Version 最需要 compact 的层级分数
    double compaction_score_ = 0.0;
    // 保存当前 Version 最需要的 compact 的层级编号
    int compaction_level_ = -1;
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

    // 指定level 当前所有 SSTable 总大小(用于计算score)
    int64_t NumLevelBytes(int level) const;
    // 判断当前 Version是否需要压缩
    bool NeedsCompaction() const;
    Compaction* PickCompaction();
    // 手动 COmpaction picker：选择指定 level/range 的重叠文件
    Compaction* CompactRange(int level, const InternalKey* begin, const InternalKey* end);
    // 为 compaction 的 inputs_[0]/inputs_[1] 创建统一有序输入 iterator
    Iterator* MakeInputIterator(Compaction* compaction);

    // 收集所有 live versions 引用的 table file number
    void AddLiveFiles(std::set<uint64_t>* live) const;

private:
    // 版本管理中的构建器
    class Builder;
    friend class Compaction;

    Status CheckComparatorName(const VersionEdit& edit) const;
    void ApplyMetadata(const VersionEdit& edit);
    static const std::vector<FileMetaData*>& LevelFiles(const Version* version, int level);

    // 计算 score & level
    void Finalize(Version* version);
    void GetRange(const std::vector<FileMetaData*>& inputs, InternalKey* smallest, InternalKey* largest) const;
    // 两组文件合并后的最大最小 key
    void GetRange2(const std::vector<FileMetaData*>& inputs1,
                   const std::vector<FileMetaData*>& inputs2,
                   InternalKey* smallest, InternalKey* largest) const;
    // 根据 inputs_[0]填充inputs[1]&grandparents_;
    void SetupOtherInputs(Compaction* compaction);

    // 把新的 Version 加入 live 链表并切换 current_
    void AppendVersion(Version* version);

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
    Version* dummy_versions_;
    Version* current_;

    // 每层下一次 size compaction 的轮转起点
    // 保存 encoded InterKey
    std::string compact_pointer_[kNumLevels];
};

}
