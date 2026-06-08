#include "Lin-DB/db/version_set.h"

#include "Lin-DB/db/table_cache.h"

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

const FileMetaData* Version::FindFileInLevel(int level, const Slice& user_key) const {
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
    const FileMetaData* file = files_[level][left];
    if (user_comparator->Compare(user_key, file->smallest.user_key()) < 0) {
        return nullptr;
    }
    return file;
}

Status Version::Get(const ReadOptions& options, const LookupKey& key, std::string* value) const {
    assert(value != nullptr);
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
    for (FileMetaData* file : candidates) {
        saver.state = kNotFound;
        Status status = table_cache_->Get(options, file->number, file->file_size, key.internal_key(), &saver, SaveValue);
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
        const FileMetaData* file = FindFileInLevel(level, key.user_key());
        if (file == nullptr) {
            continue;
        }
        saver.state = kNotFound;
        Status status = table_cache_->Get(options, file->number, file->file_size, key.internal_key(), &saver, SaveValue);
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

VersionSet::VersionSet(const std::string& dbname, const Options* options, TableCache* table_cache, 
                       const InternalKeyComparator* comparator)
    : dbname_(dbname), 
      options_(options), 
      table_cache_(table_cache), 
      comparator_(comparator), 
      next_file_number_(2), 
      log_number_(0), 
      last_sequence_(0), 
      current_(new Version(comparator, table_cache)) {
    assert(options_ != nullptr);
    assert(comparator_ != nullptr);
}  

VersionSet::~VersionSet() {
    delete current_;
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