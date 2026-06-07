#include "Lin-DB/db/version_edit.h"

#include <cassert>

#include "Lin-DB/util/coding.h"

namespace lindb {
namespace {

enum Tag : uint32_t {
    kComparator = 1,
    kLogNumber = 2,
    kNextFileNumber = 3,
    kLastSequence = 4,
    // kCompactPointer = 5,
    kDeletedFile = 6,
    kNewFile = 7,
    // kPrevLogNumber = 9,
};

bool GetLevel(Slice* input, int* level) {
    uint32_t value = 0;
    if (!GetVarint32(input, &value) || value >= static_cast<uint32_t>(kNumLevels)) {
        return false;
    }
    *level = static_cast<int>(value);
    return true;
}

bool GetInternalKey(Slice* input, InternalKey* key) {
    Slice encoded;
    ParsedInternalKey parsed;
    if (!GetLengthPrefixedSlice(input, &encoded)) {
        return false;
    }
    if (!ParseInternalKey(encoded, &parsed)) {
        return false;
    }
    return key->DecodeFrom(encoded);
}

Status InvalidVersionEdit(const Slice& message) {
    return Status::Corruption("VersionEdit", message);
}

}

VersionEdit::VersionEdit() {
    Clear();
}

void VersionEdit::Clear() {
    comparator_.clear();
    log_number_ = 0;
    next_file_number_ = 0;
    last_sequence_ = 0;
    has_comparator_ = false;
    has_log_number_ = false;
    has_next_file_number_ = false;
    has_last_sequence_ = false;
    deleted_files_.clear();
    new_files_.clear();
}

void VersionEdit::SetComparatorName(const Slice& name) {
    has_comparator_ = true;
    comparator_ = name.ToString();
}

void VersionEdit::SetLogNumber(uint64_t number) {
    has_log_number_ = true;
    log_number_ = number;
}

void VersionEdit::SetNextFile(uint64_t number) {
    has_next_file_number_ = true;
    next_file_number_ = number;
}

void VersionEdit::SetLastSequence(SequenceNumber sequence) {
    has_last_sequence_ = true;
    last_sequence_ = sequence;
}

void VersionEdit::AddFile(int level, uint64_t file_number, uint64_t file_size, 
                          const InternalKey& smallest, const InternalKey& largest) {
    assert(level >= 0 && level < kNumLevels);
    FileMetaData file;
    file.number = file_number;
    file.file_size = file_size;
    file.smallest = smallest;
    file.largest = largest;
    new_files_.push_back(std::make_pair(level, file));
}

void VersionEdit::RemoveFile(int level, uint64_t file_number) {
    assert(level >= 0 && level < kNumLevels);
    deleted_files_.insert(std::make_pair(level, file_number));
}

void VersionEdit::EncodeTo(std::string* dst) const {
    if (has_comparator_) {
        PutVarint32(dst, kComparator);
        PutLengthPrefixedSlice(dst, Slice(comparator_));
    }
    if (has_log_number_) {
        PutVarint32(dst, kLogNumber);
        PutVarint64(dst, log_number_);
    }
    if (has_next_file_number_) {
        PutVarint32(dst, kNextFileNumber);
        PutVarint64(dst, next_file_number_);
    }
    if (has_last_sequence_) {
        PutVarint32(dst, kLastSequence);
        PutVarint64(dst, last_sequence_);
    }
    for (const auto& deleted_file : deleted_files_) {
        PutVarint32(dst, kDeletedFile);
        PutVarint32(dst, static_cast<uint32_t>(deleted_file.first));
        PutVarint64(dst, deleted_file.second);
    }
    for (const auto& new_file : new_files_) {
        const FileMetaData& file = new_file.second;
        PutVarint32(dst, kNewFile);
        PutVarint32(dst, static_cast<uint32_t>(new_file.first));
        PutVarint64(dst, file.number);
        PutVarint64(dst, file.file_size);
        PutLengthPrefixedSlice(dst, file.smallest.Encode());
        PutLengthPrefixedSlice(dst, file.largest.Encode());
    }
}

Status VersionEdit::DecodeFrom(const Slice& src) {
    Clear();
    Slice input(src);
    while (!input.empty()) {
        uint32_t tag = 0;
        if (!GetVarint32(&input, &tag)) {
            return InvalidVersionEdit("missing tag");
        }

        switch (tag) {
        case kComparator: {
            Slice name;
            if (!GetLengthPrefixedSlice(&input, &name)) {
                return InvalidVersionEdit("bad comparator");
            }
            SetComparatorName(name);
            break;
        }

        case kLogNumber: {
            uint64_t number = 0;
            if (!GetVarint64(&input, &number)) {
                return InvalidVersionEdit("bad log number");
            }
            SetLogNumber(number);
            break;
        }

        case kNextFileNumber: {
            uint64_t number = 0;

            if (!GetVarint64(&input, &number)) {
                return InvalidVersionEdit("bad next file number");
            }
            SetNextFile(number);
            break;
        }

        case kLastSequence: { 
            uint64_t sequence = 0;
            if (!GetVarint64(&input, &sequence)) {
                return InvalidVersionEdit("bad last sequence");
            }
            SetLastSequence(sequence);
            break;
        }

        case kDeletedFile: {
            int level = 0;
            uint64_t file_number = 0;
            if (!GetLevel(&input, &level) || !GetVarint64(&input, &file_number)) {
                return InvalidVersionEdit("bad deleted file");
            }
            RemoveFile(level, file_number);
            break;
        }

        case kNewFile: {
            int level = 0;
            uint64_t file_number = 0;
            uint64_t file_size = 0;
            InternalKey smallest;
            InternalKey largest;
            if (!GetLevel(&input, &level) ||
                !GetVarint64(&input, &file_number) ||
                !GetVarint64(&input, &file_size) ||
                !GetInternalKey(&input, &smallest) ||
                !GetInternalKey(&input, &largest)) {
                    return InvalidVersionEdit("bad new file");
            }
            AddFile(level, file_number, file_size, smallest, largest);
            break;
        }
        
        default:
            return InvalidVersionEdit("unknown tag");
        }
    }
    return Status::OK();
}

}