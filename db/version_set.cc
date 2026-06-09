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

}

Version::Version(const InternalKeyComparator* comparator, TableCache* table_cache)
    : comparator_(comparator), 
      table_cache_(table_cache) {
    assert(comparator_ != nullptr);
}
Version::Version(const Version& other)
    : comparator_(other.comparator_), 
      table_cache_(other.table_cache_) {
    assert(comparator_ != nullptr);
    for (int level_index = 0; level_index < kNumLevels; ++level_index) {
        for (const FileMetaData* file : other.files_[level_index]) {
            files_[level_index].push_back(new FileMetaData(*file));
        }
    }
}
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
    next_file_number_ = 2;
    log_number_ = 0;
    last_sequence_ = 0;

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
    Version* next = new Version(*current_);
    for (const auto& deleted_file : edit->deleted_files()) {
        next->RemoveFile(deleted_file.first, deleted_file.second);
    }
    for (const auto& new_file : edit->new_files()) {
        next->AddFile(new_file.first, new_file.second);
    }
    next->SortFiles();
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

}