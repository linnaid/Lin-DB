#include "Lin-DB/db/version_set.h"

#include <algorithm>
#include <cassert>

namespace lindb {
Version::Version(const InternalKeyComparator* comparator)
    : comparator_(comparator) {
    assert(comparator_ != nullptr);
}
Version::Version(const Version& other)
    : comparator_(other.comparator_) {
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

VersionSet::VersionSet(const std::string& dbname, const Options* options, TableCache* table_cache, 
                       const InternalKeyComparator* comparator)
    : dbname_(dbname), 
      options_(options), 
      table_cache_(table_cache), 
      comparator_(comparator), 
      next_file_number_(2), 
      log_number_(0), 
      last_sequence_(0), 
      current_(new Version(comparator)) {
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