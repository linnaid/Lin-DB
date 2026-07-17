#include "Lin-DB/db/version_set.h"

#include <lindb/env.h>
#include <lindb/filename.h>

#include "Lin-DB/db/table_cache.h"
#include "Lin-DB/db/log_reader.h"
#include "Lin-DB/db/log_writer.h"

#include <algorithm>
#include <cassert>

namespace lindb {
namespace {
enum SaverState {
    kNotFound,
    kFound,
    kDeleted,
    kCorrupt,
};

struct Saver {
    SaverState state;
    const Comparator* user_comparator;
    Slice user_key;
    std::string* value;
};

void SaveValue(void* arg, const Slice& internal_key, const Slice& value) {
    Saver* saver = reinterpret_cast<Saver*>(arg);
    ParsedInternalKey parsed_key;
    if (!ParseInternalKey(internal_key, &parsed_key)) {
        saver->state = kCorrupt;
        return;
    }
    if (saver->user_comparator->Compare(parsed_key.user_key, saver->user_key)!= 0) {
        return;
    }
    if (parsed_key.type == kTypeValue) {
        saver->state = kFound;
        saver->value->assign(value.data(), value.size());
        return;
    }
    if (parsed_key.type == kTypeDeletion) {
        saver->state = kDeleted;
        return;
    }
    saver->state = kCorrupt;
}

bool NewestFileFirst(const FileMetaData* left, const FileMetaData* right) {
    return left->number > right->number;
}

constexpr int kL0CompactionTrigger = 4;

int64_t TotalFileSize(const std::vector<FileMetaData*>& files) {
    int64_t total = 0;
    for (const FileMetaData* file : files) {
        total += static_cast<int64_t>(file->file_size);
    }
    return total;
}

// 某层触发压缩的字节阈值
double MaxBytesForLevel(const Options* options, int level) {
    (void)options; // 暂时不用options，保留用于后续接入 LevelDB 策略
    double result = 10.0 * 1048576.0;
    while (level > 1) {
        result *= 10.0;
        -- level;
    }
    return result;
}

// 扩大compaction 时输入允许的最大字节数
int64_t ExpandedCompactionByteSizeLimit(const Options* options) {
    return 25 * static_cast<int64_t>(options->max_file_size);
}

bool FindLargestKey(const InternalKeyComparator& comparator, 
                    const std::vector<FileMetaData*>& files, 
                    InternalKey* largest_key) {
    if (files.empty()) {
        return false;
    }
    *largest_key = files[0]->largest;
    for (size_t index = 1; index < files.size(); ++index) {
        FileMetaData* file = files[index];
        if (comparator.Compare(file->largest, *largest_key) > 0) {
            *largest_key = file->largest;
        }
    }
    return true;
}

FileMetaData* FindSmallestBoundaryFile(const InternalKeyComparator& comparator, 
                                       const std::vector<FileMetaData*>& level_files, 
                                       const InternalKey& largest_key) {
    const Comparator* user_comparator = comparator.user_comparator();
    FileMetaData* smallest_boundary_file = nullptr;
    for (FileMetaData* file : level_files) {
        if (comparator.Compare(file->smallest, largest_key) > 0 && 
            user_comparator->Compare(file->smallest.user_key(), largest_key.user_key()) == 0) {
            if (smallest_boundary_file == nullptr || 
                comparator.Compare(file->smallest, smallest_boundary_file->smallest) < 0) {
                smallest_boundary_file = file;
            }
        }
    }
    return smallest_boundary_file;
}

// 对被选压缩文件层做范围扩展，避免连续边界文件被拆开 compaction
// 减少闭包扩展的次数，避免因为边界问题导致 compaction的user_key范围持续扩大
void AddBoundaryInputs(const InternalKeyComparator& comparator, 
                       const std::vector<FileMetaData*>& level_files, 
                       std::vector<FileMetaData*>* compaction_files) {
    InternalKey largest_key;
    if (!FindLargestKey(comparator, *compaction_files, &largest_key)) {
        return;
    }
    while (true) {
        FileMetaData* boundary_file = FindSmallestBoundaryFile(comparator, level_files, largest_key);
        if (boundary_file == nullptr) {
            break;
        }
        compaction_files->push_back(boundary_file);
        largest_key = boundary_file->largest;
    }
}

}

Compaction::Compaction(int level) 
    : level_(level) {
        assert(level_ >= 0);
        assert(level_ + 1 < kNumLevels);
}

int Compaction::level() const {
    return level_;
}

int Compaction::num_input_files(int which) const {
    assert(which >= 0 && which < 2);
    return static_cast<int>(inputs_[which].size());
}

const FileMetaData* Compaction::input(int which, int index) const {
    assert(which >= 0 && which < 2);
    assert(index >= 0);
    assert(static_cast<size_t>(index) < inputs_[which].size());

    return inputs_[which][index];
}

int Compaction::num_grandparents() const {
    return static_cast<int>(grandparents_.size());
}

const FileMetaData* Compaction::grandparent(int index) const {
    assert(index >= 0);
    assert(static_cast<size_t>(index) < grandparents_.size());

    return grandparents_[index];
}

Version::Version(const InternalKeyComparator* comparator, TableCache* table_cache)
    : comparator_(comparator), 
      table_cache_(table_cache) {
    assert(comparator_ != nullptr);
}
Version::Version(const Version& other)
    : comparator_(other.comparator_), 
      table_cache_(other.table_cache_), 
      compaction_level_(other.compaction_level_), 
      compaction_score_(other.compaction_score_) {
    assert(comparator_ != nullptr);
    for (int level_index = 0; level_index < kNumLevels; ++level_index) {
        for (const FileMetaData* file : other.files_[level_index]) {
            files_[level_index].push_back(new FileMetaData(*file));
        }
    }
}
// 修改点1
// 需要修改file同步部分，和LevelDB同步
Version& Version::operator=(const Version& other) {
    if (this == &other) {
        return *this;
    }
    for (int level_index = 0; level_index < kNumLevels; ++level_index) {
        for (FileMetaData* file : files_[level_index]) {
            delete file;
        }
        files_[level_index].clear();
    }
    comparator_ = other.comparator_;
    table_cache_ = other.table_cache_;
    file_to_compact_ = nullptr;
    file_to_compact_level_ = -1;
    compaction_level_ = other.compaction_level_;
    compaction_score_ = other.compaction_score_;
    assert(comparator_ != nullptr);
    for (int level_index = 0; level_index < kNumLevels; ++level_index) {
        for (const FileMetaData* file : other.files_[level_index]) {
            files_[level_index].push_back(new FileMetaData(*file));
        }
    }
    return *this;
}

Version::~Version() {
    for (int level_index = 0; level_index < kNumLevels; ++level_index) {
        for (FileMetaData* file : files_[level_index]) {
            delete file;
        }
    }
}

int Version::NumFiles(int level) const {
    assert(level >= 0 && level < kNumLevels);
    return static_cast<int>(files_[level].size());
}

const FileMetaData* Version::File(int level, size_t index) const {
    assert(level >= 0 && level < kNumLevels);
    assert(index < files_[level].size());
    return files_[level][index];
}

void Version::AddFile(int level, const FileMetaData& file) {
    assert(level >= 0 && level < kNumLevels);
    RemoveFile(level, file.number);
    files_[level].push_back(new FileMetaData(file));
}

void Version::RemoveFile(int level, uint64_t file_number) {
    assert(level >= 0 && level < kNumLevels);
    std::vector<FileMetaData*>& files = files_[level];
    for (auto iter = files.begin(); iter != files.end(); ) {
        if ((*iter)->number == file_number) {
            delete *iter;
            iter = files.erase(iter);
        } else {
            ++iter;
        }
    }
}

void Version::SortFiles() {
    for (int level_index = 0; level_index < kNumLevels; ++level_index) {
        std::sort(files_[level_index].begin(), files_[level_index].end(), 
                  [this](const FileMetaData* left, const FileMetaData* right) {
                    return comparator_->Compare(left->smallest, right->smallest) < 0;
                  });
    }
}

void Version::GetOverlappingInputs(int level, const InternalKey* begin, 
                                   const InternalKey* end, std::vector<FileMetaData*>* inputs) const {
    assert(level >= 0 && level < kNumLevels);
    assert(inputs != nullptr);
    inputs->clear();

    Slice user_begin;
    Slice user_end;
    bool has_begin = false;
    bool has_end = false;
    if (begin != nullptr) {
        user_begin = begin->user_key();
        has_begin = true;
    }
    if (end != nullptr) {
        user_end = end->user_key();
        has_end = true;
    }

    const Comparator* user_comparator = comparator_->user_comparator();
    for (size_t index = 0; index < files_[level].size(); ) {
        FileMetaData* file = files_[level][index++];
        const Slice file_start = file->smallest.user_key();
        const Slice file_limit = file->largest.user_key();
        if (has_begin && user_comparator->Compare(file_limit, user_begin) < 0) {
            continue;
        }
        if (has_end && user_comparator->Compare(file_start, user_end) > 0) {
            continue;
        }

        inputs->push_back(file);
        // 一次循环只允许扩展一次边界
        if (level == 0) {
            if (has_begin && user_comparator->Compare(file_start, user_begin) < 0) {
                user_begin = file_start;
                inputs->clear();
                index = 0;
            } else if (has_end && user_comparator->Compare(file_limit, user_end) > 0) {
                user_end = file_limit;
                inputs->clear();
                index = 0;
            }
        }
    }
}

FileMetaData* Version::FindFileInLevel(int level, const Slice& user_key) const {
    assert(level > 0 && level < kNumLevels);
    const Comparator* user_comparator = comparator_->user_comparator();
    size_t left = 0;
    size_t right = files_[level].size();
    while (left < right) {
        const size_t middle = left + (right - left)/2;
        const FileMetaData* file = files_[level][middle];
        if (user_comparator->Compare(file->largest.user_key(), user_key) < 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }
    if (left >= files_[level].size()) {
        return nullptr;
    }
    FileMetaData* file = files_[level][left];
    if (user_comparator->Compare(user_key, file->smallest.user_key()) < 0) {
        return nullptr;
    }
    return file;
}

Status Version::Get(const ReadOptions& options, const LookupKey& key, std::string* value) const {
    GetStats stats;
    return Get(options, key, value, &stats);
}

Status Version::Get(const ReadOptions& options, const LookupKey& key, std::string* value, GetStats* stats) const {
    assert(value != nullptr);
    assert(stats != nullptr);
    stats->seek_file = nullptr;
    stats->seek_file_level = -1;
    if (table_cache_ == nullptr) {
        return Status::NotFound(key.user_key());
    }
    
    std::vector<FileMetaData*> candidates;
    for (FileMetaData* file : files_[0]) {
        if (FileMayContainUserkey(file, key.user_key())) {
            candidates.push_back(file);
        }
    }
    std::sort(candidates.begin(), candidates.end(), NewestFileFirst);

    Saver saver;
    saver.user_comparator = comparator_->user_comparator();
    saver.user_key = key.user_key();
    saver.value = value;

    FileMetaData* last_file_read = nullptr;
    int last_file_read_level = -1;

    auto read_file = [&](FileMetaData* file, int level) -> Status {
        if (stats->seek_file == nullptr && last_file_read != nullptr) {
            stats->seek_file = last_file_read;
            stats->seek_file_level = last_file_read_level;
        }
        last_file_read = file;
        last_file_read_level = level;
        saver.state = kNotFound;
        return table_cache_->Get(options, file->number, file->file_size, key.internal_key(), &saver, SaveValue);
    };

    for (FileMetaData* file : candidates) {
        saver.state = kNotFound;
        Status status = read_file(file, 0);
        // Status status = table_cache_->Get(options, file->number, file->file_size, key.internal_key(), &saver, SaveValue);
        if (!status.ok()) {
            return status;
        }
        if (saver.state == kFound) {
            return Status::OK();
        }
        if (saver.state == kDeleted) {
            return Status::NotFound(key.user_key());
        }
        if (saver.state == kCorrupt) {
            return Status::Corruption("corrupted key for", key.user_key());
        }
    }

    for (int level = 1; level < kNumLevels; ++level) {
        FileMetaData* file = FindFileInLevel(level, key.user_key());
        if (file == nullptr) {
            continue;
        }
        Status status = read_file(file, level);
        // saver.state = kNotFound;
        // Status status = table_cache_->Get(options, file->number, file->file_size, key.internal_key(), &saver, SaveValue);
        if (!status.ok()) {
            return status;
        }
        if (saver.state == kFound) {
            return Status::OK();
        }
        if (saver.state == kDeleted) {
            return Status::NotFound(key.user_key());
        }
        if (saver.state == kCorrupt) {
            return Status::Corruption("corrupted key for", key.user_key());
        }
    }
    
    return Status::NotFound(key.user_key());
}

void Version::AddIterators(const ReadOptions& options, std::vector<Iterator*>* iters) const {
    assert(iters != nullptr);
    if (table_cache_ == nullptr) {
        return;
    }
    for (int level = 0; level < kNumLevels; ++level) {
        for (FileMetaData* file : files_[level]) {
            iters->push_back(table_cache_->NewIterator(options, file->number, file->file_size));
        }
    }
}

bool Version::FileMayContainUserkey(const FileMetaData* file, const Slice& user_key) const {
    assert(file != nullptr);
    const Comparator* user_comparator = comparator_->user_comparator();
    if (user_comparator->Compare(user_key, file->smallest.user_key()) < 0) {
        return false;
    }
    if (user_comparator->Compare(user_key, file->largest.user_key()) > 0) {
        return false;
    }
    return true;
}

bool Version::UpdateStats(const GetStats& stats) {
    FileMetaData* file = stats.seek_file;
    if (file != nullptr) {
        --file->allowed_seeks;
        if (file->allowed_seeks <= 0 && file_to_compact_ == nullptr) {
            file_to_compact_ = file;
            file_to_compact_level_ = stats.seek_file_level;
            return true;
        }
    }
    return false;
}

FileMetaData* Version::FileToCompact() const {
    return file_to_compact_;
}

int Version::FileToCompactLevel() const {
    return file_to_compact_level_;
}

int Version::CompactionLevel() const {
    return compaction_level_;
}

double Version::CompactionScore() const {
    return compaction_score_;
}

// 修改点2
// LevelDB 使用的是双向链表，当current变化时，旧 Version 无法保存
// 在这里调用Finalize 没有意义，current_ 初始时是空，Recover()后才有数据
VersionSet::VersionSet(const std::string& dbname, const Options* options, TableCache* table_cache, 
                       const InternalKeyComparator* comparator)
    : env_(options->env), 
      dbname_(dbname), 
      options_(options), 
      table_cache_(table_cache), 
      comparator_(comparator), 
      next_file_number_(2), 
      log_number_(0), 
      last_sequence_(0), 
      manifest_file_number_(0), 
      descriptor_file_(nullptr), 
      descriptor_log_(nullptr), 
      current_(new Version(comparator, table_cache)) {
    assert(options_ != nullptr);
    assert(comparator_ != nullptr);
    Finalize(current_);
}  

VersionSet::~VersionSet() {
    delete current_;
    descriptor_log_.reset();
    if (descriptor_file_ != nullptr) {
        (void)descriptor_file_->Close();
        delete descriptor_file_;
        descriptor_file_ = nullptr;
    }
}

Status VersionSet::LogAndApply(VersionEdit* edit) {
    assert(edit != nullptr);
    Status status = CheckComparatorName(*edit);
    if (!status.ok()) { return status; }
    
    bool created_manifest = false;
    std::string manifest_name;

    if (descriptor_log_ == nullptr) {
        if (manifest_file_number_ == 0) {
            manifest_file_number_ = NewFileNumber();
        }
        manifest_name = DescriptorFileName(dbname_, manifest_file_number_);
        status = env_->NewWritableFile(manifest_name, &descriptor_file_);
        if (status.ok()) {
            descriptor_log_ = std::make_unique<log::Writer>(descriptor_file_);
        }
        if (status.ok()) {
            status = WriteSnapshot(descriptor_log_.get());
        }
        created_manifest = status.ok();
    }

    if (status.ok() && !edit->has_comparator()) {
        edit->SetComparatorName(Slice(comparator_->Name()));
    }
    if (status.ok() && !edit->has_log_number()) {
        edit->SetLogNumber(log_number_);
    }
    if (status.ok() && !edit->has_next_file_number()) {
        edit->SetNextFile(next_file_number_);
    }
    if (status.ok() && !edit->has_last_sequence()) {
        edit->SetLastSequence(last_sequence_);
    }

    std::string record;
    if (status.ok()) {
        edit->EncodeTo(&record);
    }
    if (status.ok()) {
        status = descriptor_log_->AddRecord(Slice(record));
    }
    if (status.ok()) {
        status = descriptor_file_->Sync();
    }
    if (status.ok() && created_manifest) {
        status = SetCurrentFile(env_, dbname_, manifest_file_number_);
    }
    if (status.ok()) {
        status = ApplyVersionEdit(edit);
    }

    if (!status.ok() && created_manifest) {
        descriptor_log_.reset();
        delete descriptor_file_;
        descriptor_file_ = nullptr;
        env_->RemoveFile(manifest_name);
    }
    return status;
}

Status VersionSet::Recover() {
    std::string current;
    Status status = ReadFileToString(env_, CurrentFileName(dbname_), &current);
    if (!status.ok()) {
        return status;
    }
    if (!current.empty() && current.back() == '\n') {
        current.pop_back();
    }
    if (current.empty()) {
        return Status::Corruption("CURRENT file is empty");
    }

    uint64_t manifest_number = 0;
    FileType file_type;
    if (!ParseFileName(current, &manifest_number, &file_type) || file_type != kDescriptorFile) {
        return Status::Corruption("CURRENT points to invalid MANIFEST", current);
    }

    SequentialFile* file = nullptr;
    status = env_->NewSequentialFile(dbname_ + "/" + current, &file);
    if (!status.ok()) {
        return status;
    }

    delete current_;
    current_ = new Version(comparator_, table_cache_);
    Finalize(current_);
    // LevelDB会检查是否存在关键元数据(lognumber,nextfilenumber)
    // 缺失则设为 corruption 并终止 Recover
    next_file_number_ = 2;
    log_number_ = 0;
    last_sequence_ = 0;

    for (int level = 0; level < kNumLevels; ++level) {
        compact_pointer_[level].clear();
    }

    log::Reader reader(file, true);
    std::string record;
    Status read_status;
    while (reader.ReadRecord(&record, &read_status)) {
        VersionEdit edit;
        status = edit.DecodeFrom(Slice(record));
        if (!status.ok()) { break; }
        status = ApplyVersionEdit(&edit);
        if (!status.ok()) { break; }
    }

    if (status.ok() && !read_status.ok()) {
        status = read_status;
    }
    delete file;
    if (status.ok()) {
        manifest_file_number_ = manifest_number;
    }
    return status;
}

Status VersionSet::WriteSnapshot(log::Writer* writer) const {
    assert(writer != nullptr);
    VersionEdit snapshot;
    snapshot.SetComparatorName(Slice(comparator_->Name()));
    snapshot.SetLastSequence(last_sequence_);

    for (int level = 0; level < kNumLevels; ++level) {
        if (!compact_pointer_[level].empty()) {
            InternalKey key;
            key.DecodeFrom(Slice(compact_pointer_[level]));
            snapshot.SetCompactPointer(level, key);
        }
    }

    for (int level = 0; level < kNumLevels; ++level) {
        for (int index = 0; index < current_->NumFiles(level); ++index) {
            const FileMetaData* file = current_->File(level, index);
            snapshot.AddFile(level, file->number, file->file_size, file->smallest, file->largest);
        }
    }
    std::string record;
    snapshot.EncodeTo(&record);
    return writer->AddRecord(Slice(record));
}

Status VersionSet::ApplyVersionEdit(VersionEdit* edit) {
    assert(edit != nullptr);
    Status status = CheckComparatorName(*edit);
    if (!status.ok()) {
        return status;
    }

    for (const auto& compact_pointer : edit->compact_pointers()) {
        const int level = compact_pointer.first;
        compact_pointer_[level] = compact_pointer.second.Encode().ToString();
    }

    Version* next = new Version(*current_);
    for (const auto& deleted_file : edit->deleted_files()) {
        next->RemoveFile(deleted_file.first, deleted_file.second);
    }
    for (const auto& new_file : edit->new_files()) {
        next->AddFile(new_file.first, new_file.second);
    }
    next->SortFiles();
    Finalize(next);
    ApplyMetadata(*edit);
    for (const auto& new_file : edit->new_files()) {
        MarkFileNumberUsed(new_file.second.number);
    }
    delete current_;
    current_ = next;
    return Status::OK();
}

Version* VersionSet::current() const {
    return current_;
}

uint64_t VersionSet::NewFileNumber() {
    return next_file_number_++;
}

void VersionSet::MarkFileNumberUsed(uint64_t number) {
    if (next_file_number_ <= number) {
        next_file_number_ = number + 1;
    }
}

SequenceNumber VersionSet::LastSequence() const {
    return last_sequence_;
}

uint64_t VersionSet::LogNumber() const {
    return log_number_;
}

int64_t VersionSet::NumLevelBytes(int level) const {
    assert(level >= 0 && level < kNumLevels);
    return TotalFileSize(current_->files_[level]);
}

bool VersionSet::NeedsCompaction() const {
    return current_->compaction_score_ >= 1.0 || current_->file_to_compact_ != nullptr;
}

Compaction* VersionSet::PickCompaction() {
    const bool size_compaction = current_->compaction_score_ >= 1.0;
    const bool seek_compaction = current_->file_to_compact_ != nullptr;
    int level = -1;
    Compaction* compaction = nullptr;

    // 修改点4
    // 这里当进入 size compaction 时，应该先遍历该层各个文件，若未找到再赋值为files_[level][0]
    // 不过现在是 10.1，预计后期会补上
    if (size_compaction) {
        level = current_->compaction_level_;
        // assert(level >= 0);
        // assert(level + 1 < kNumLevels);
        if (level < 0 || level + 1 >= kNumLevels || current_->files_[level].empty()) {
            return nullptr;
        }
        compaction = new Compaction(level);

        // 利用回绕使得在找不到制定文件时可以从头开始进行压缩(wrap-around)
        for (FileMetaData* file : current_->files_[level]) {
            if (compact_pointer_[level].empty() || 
                comparator_->Compare(file->largest.Encode(), Slice(compact_pointer_[level])) > 0) {
                compaction->inputs_[0].push_back(file);
                break;
            } 
        }
        if (compaction->inputs_[0].empty()) {
            compaction->inputs_[0].push_back(current_->files_[level][0]);
        }

    } else if (seek_compaction) {
        level = current_->file_to_compact_level_;
        if (level < 0 || level + 1 >= kNumLevels) {
            return nullptr;
        }
        compaction = new Compaction(level);

        compaction->inputs_[0].push_back(current_->file_to_compact_);
    } else {
        return nullptr;
    }

    if (level == 0) {
        InternalKey smallest;
        InternalKey largest;
        GetRange(compaction->inputs_[0], &smallest, &largest);
        current_->GetOverlappingInputs(0, &smallest, &largest, &compaction->inputs_[0]);
        if (compaction->inputs_[0].empty()) {
            delete compaction;
            return nullptr;
        }
    }

    SetupOtherInputs(compaction);
    return compaction;
}

Status VersionSet::CheckComparatorName(const VersionEdit& edit) const {
    if (!edit.has_comparator()) {
        return Status::OK();
    }
    if (edit.comparator_name() == comparator_->Name()) {
        return Status::OK();
    }
    return Status::InvalidArgument("VersionSet comparator mismatch", edit.comparator_name());
}

void VersionSet::ApplyMetadata(const VersionEdit& edit) {
    if (edit.has_log_number()) {
        log_number_ = edit.log_number();
    }
    if (edit.has_next_file_number() && next_file_number_ < edit.next_file_number()) {
        next_file_number_ = edit.next_file_number();
    }
    if (edit.has_last_sequence() && last_sequence_ < edit.last_sequence()) {
        last_sequence_ = edit.last_sequence();
    }
}

void VersionSet::Finalize(Version* version) {
    assert(version != nullptr);
    int best_level = -1;
    double best_score = -1.0;
    for (int level = 0; level < kNumLevels; ++level) {
        double score = 0.0;
        if (level == 0) {
            score = version->files_[level].size() / static_cast<double>(kL0CompactionTrigger);
        } else {
            score = static_cast<double>(TotalFileSize(version->files_[level])) / MaxBytesForLevel(options_, level);
        }
        if (score > best_score) {
            best_level = level;
            best_score = score;
        }
    }
    version->compaction_level_ = best_level;
    version->compaction_score_ = best_score;
}

void VersionSet::GetRange(const std::vector<FileMetaData*>& inputs, InternalKey* smallest, InternalKey* largest) const {
    assert(!inputs.empty());
    // assert(smallest != nullptr);
    // assert(largest != nullptr);
    *smallest = inputs[0]->smallest;
    *largest = inputs[0]->largest;
    for (size_t index = 1; index < inputs.size(); ++index) {
        FileMetaData* file = inputs[index];
        if (comparator_->Compare(file->smallest, *smallest) < 0) {
            *smallest = file->smallest;
        }
        if (comparator_->Compare(file->largest, *largest) > 0) {
            *largest = file->largest;
        }
    }
}

void VersionSet::GetRange2(const std::vector<FileMetaData*>& inputs1, 
                           const std::vector<FileMetaData*>& inputs2, 
                           InternalKey* smallest, InternalKey* largest) const {
    std::vector<FileMetaData*> all = inputs1;
    all.insert(all.end(), inputs2.begin(), inputs2.end());
    GetRange(all, smallest, largest);
}


// 修改点5
// 目前10.1,后续或许会补上部分相应功能，缺失部分：
// 2. Expand Inputs，扩大输入，减少写放大
// 3. 更新compact_pointer_，也就是一个轮转选择压缩起点
void VersionSet::SetupOtherInputs(Compaction* compaction) {
    assert(compaction != nullptr);
    const int level = compaction->level();
    // 本层最大最小
    InternalKey smallest;
    InternalKey largest;

    AddBoundaryInputs(*comparator_, current_->files_[level], &compaction->inputs_[0]);
    GetRange(compaction->inputs_[0], &smallest, &largest);

    current_->GetOverlappingInputs(level + 1, &smallest, &largest, &compaction->inputs_[1]);
    AddBoundaryInputs(*comparator_, current_->files_[level+1], &compaction->inputs_[1]);

    // 两层合并最大最小
    InternalKey all_start;
    InternalKey all_limit;
    GetRange2(compaction->inputs_[0], compaction->inputs_[1], &all_start, &all_limit);

    // 只有存在下一层的输入时，扩展本层输入才有减少写放大的意义
    if (!compaction->inputs_[1].empty()) {
        std::vector<FileMetaData*> expanded0;
        current_->GetOverlappingInputs(level, &all_start, &all_limit, &expanded0);
        AddBoundaryInputs(*comparator_, current_->files_[level], &expanded0);
        
        // const int64_t inputs0_size = TotalFileSize(compaction->inputs_[0]);
        const int64_t inputs1_size = TotalFileSize(compaction->inputs_[1]);
        const int64_t expanded0_size = TotalFileSize(expanded0);
        // (void)inputs0_size; // 当前没有 info_log 输出，只用于未来日志，可以暂时不要
        if (expanded0.size() > compaction->inputs_[0].size() && 
            inputs1_size + expanded0_size < ExpandedCompactionByteSizeLimit(options_)) {
            InternalKey new_start;
            InternalKey new_limit;
            GetRange(expanded0, &new_start, &new_limit);
            
            std::vector<FileMetaData*> expanded1;
            current_->GetOverlappingInputs(level+1, &new_start, &new_limit, &expanded1);
            AddBoundaryInputs(*comparator_, current_->files_[level+1], &expanded1);

            if (expanded1.size() == compaction->inputs_[1].size()) {
                smallest = new_start;
                largest = new_limit;
                compaction->inputs_[0] = expanded0;
                compaction->inputs_[1] = expanded1;
                GetRange2(compaction->inputs_[0], compaction->inputs_[1], &all_start, &all_limit);
            }
        }
    }

    // 如果存在 grandparent 记录它的 overlap，后续compaction输出切分会用到
    if (level + 2 < kNumLevels) {
        current_->GetOverlappingInputs(level + 2, &all_start, &all_limit, &compaction->grandparents_);
    }

    compact_pointer_[level] = largest.Encode().ToString();
}

}