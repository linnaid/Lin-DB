#include  "Lin-DB/db/db_impl.h"

#include <cassert>
#include <utility>
#include <vector>
#include <algorithm>
#include <set>

#include <lindb/write_batch.h>
#include <lindb/env.h>
#include <lindb/filename.h>
#include <lindb/iterator.h>

#include "Lin-DB/db/write_batch_internal.h"
#include "Lin-DB/db/db_iter.h"
#include "Lin-DB/db/merger.h"
#include "Lin-DB/db/table_cache.h"
#include "Lin-DB/db/version_set.h"
#include "Lin-DB/db/builder.h"
#include "Lin-DB/db/version_edit.h"
#include "Lin-DB/db/log_reader.h"

namespace lindb {
SnapshotImpl::SnapshotImpl(SequenceNumber sequence)
    : sequence_(sequence) {}

SequenceNumber SnapshotImpl::sequence() const {
    return sequence_;
}

namespace {
Options MakeTableOptions(const Options& options, const InternalKeyComparator* internal_comparator) {
    Options table_options = options;
    table_options.comparator = internal_comparator;
    return table_options;
}

int TableCacheEntries(int max_open_files) {
    const int reserved_files = 10;
    if (max_open_files > reserved_files) {
        return max_open_files - reserved_files;
    }
    return 1;
}

constexpr size_t kWriteBatchHeaderSize = 12;

// 判断目录枚举结果是否是 POSIX 特殊目录项("."/"..")
bool IsDotOrDotDot(const std::string& filename) {
    return filename == "." || filename == "..";
}

}


DBImpl::DBImpl(const Options& options, std::string dbname)
    : options_(options),
      dbname_(std::move(dbname)),
      internal_comparator_(options_.comparator),
      table_options_(MakeTableOptions(options_, &internal_comparator_)), 
      table_cache_(dbname_, table_options_, TableCacheEntries(options_.max_open_files)),  
      versions_(dbname_, &table_options_, &table_cache_, &internal_comparator_), 
      mem_(std::make_unique<MemTable>(internal_comparator_)),
      imm_(nullptr), 
      last_sequence_(0), 
      logfile_number_(1) {}

DBImpl::~DBImpl() {
    log_.reset();
    if (log_file_ != nullptr) {
        (void)log_file_->Close();
        delete log_file_;
        log_file_ = nullptr;
    }
}

Status DBImpl::InitWAL() {
    if (!options_.env->FileExists(dbname_)) {
        Status s = options_.env->CreateDir(dbname_);
        if (!s.ok()) {
            return s;
        }
    }

    const std::string log_name = LogFileName(dbname_, logfile_number_);
    WritableFile* file = nullptr;
    Status s = options_.env->NewWritableFile(log_name, &file);
    if (!s.ok()) {
        return s;
    }

    log_file_ = file;
    log_ = std::make_unique<log::Writer>(log_file_);
    return Status::OK();
}

Status DBImpl::Put(const WriteOptions& options, const Slice& key, const Slice& value) {
    // (void)options;
    WriteBatch batch;
    batch.Put(key, value);
    return Write(options, &batch);

    // const SequenceNumber sequence = NewSequence();
    // mem_.Add(sequence, kTypeValue, key, value);
    // return Status::OK();
}

Status DBImpl::Delete(const WriteOptions& options, const Slice& key) {
    WriteBatch batch;
    batch.Delete(key);
    return Write(options, &batch);
}

Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
    if (updates == nullptr) {
        return Status::InvalidArgument("DBImpl::Write updates is null");
    }

    const int count = WriteBatchInternal::Count(updates);
    if (count == 0) {
        return Status::OK();
    }

    Status s = MakeRoomForWrite();
    if (!s.ok()) {
        return s;
    }

    assert(last_sequence_ <= kMaxSequenceNumber - static_cast<SequenceNumber>(count));
    const SequenceNumber first_sequence = last_sequence_ + 1;
    WriteBatchInternal::SetSequence(updates, first_sequence);

    s = log_->AddRecord(WriteBatchInternal::Contents(updates));
    if (!s.ok()) {
        return s;
    }

    if (options.sync) {
        s = log_file_->Sync();
        if (!s.ok()) {
            return s;
        }
    }

    s = WriteBatchInternal::InsertInto(updates, mem_.get());
    if(s.ok()) {
        last_sequence_ += static_cast<SequenceNumber>(count);
    }

    return s;
}

Status DBImpl::Get(const ReadOptions& options, const Slice& key, std::string* value) {
    if (value == nullptr) {
        return Status::InvalidArgument("DBImpl::Get value is null");
    }

    LookupKey lookup_key(key, GetSnapshotSequence(options));
    Status status;

    if(mem_->Get(lookup_key, value, &status)) {
        return status;
    }

    if (imm_ != nullptr && imm_->Get(lookup_key, value, &status)) {
        return status;
    }

    status = versions_.current()->Get(options, lookup_key, value);
    if (!status.ok()) {
        value->clear();
    }
    return status;
}

Iterator* DBImpl::NewIterator(const ReadOptions& options) {
    SequenceNumber sequence = GetSnapshotSequence(options);
    std::vector<Iterator*> children;
    children.push_back(mem_->NewIterator());
    if (imm_ != nullptr) {
        children.push_back(imm_->NewIterator());
    }
    versions_.current()->AddIterators(options, &children);
    Iterator* internal_iter = NewMergingIterator(&internal_comparator_, std::move(children));
    return NewDBIterator(options_.comparator, internal_iter, sequence);
}

const Snapshot* DBImpl::GetSnapshot() {
    return new SnapshotImpl(last_sequence_);
}

void DBImpl::ReleaseSnapshot(const Snapshot* snapshot) {
    delete snapshot;
}

SequenceNumber DBImpl::GetSnapshotSequence(const ReadOptions& options) const {
    if (options.snapshot == nullptr) {
        return last_sequence_;
    }
    const SnapshotImpl* snapshot = static_cast<const SnapshotImpl*>(options.snapshot);
    return snapshot->sequence();
}

SequenceNumber DBImpl::NewSequence() {
    assert(last_sequence_ < kMaxSequenceNumber);
    ++last_sequence_;
    return last_sequence_;
}

// 打开 DB 时恢复 MANIFEST、重放 WAL，并创建新的当前 WAL
Status DBImpl::Open() {
    if (!options_.env->FileExists(dbname_)) {
        Status status = options_.env->CreateDir(dbname_);
        if (!status.ok()) {
            return status;
        }
    }

    std::vector<std::string> filenames;
    Status status = options_.env->GetChildren(dbname_, &filenames);
    if(!status.ok()) {
        return status;
    }

    const bool has_current = options_.env->FileExists(CurrentFileName(dbname_));
    if (has_current) {
        status = versions_.Recover();
        if (!status.ok()) {
            return status;
        }
        last_sequence_ = versions_.LastSequence();
    }

    std::vector<uint64_t> log_numbers;
    for (const std::string& filename : filenames) {
        if (IsDotOrDotDot(filename)) {
            continue;
        }

        uint64_t number = 0; 
        FileType type;
        if (!ParseFileName(filename, &number, &type)) {
            continue;
        }

        if (number > 0) {
            versions_.MarkFileNumberUsed(number);
        }

        if (type == kLogFile && (!has_current || number >= versions_.LogNumber())) {
            log_numbers.push_back(number);
        }
    }

    std::sort(log_numbers.begin(), log_numbers.end());
    for (uint64_t log_number : log_numbers) {
        logfile_number_ = log_number;
        status = RecoverLogFile(log_number);
        if (!status.ok()) {
            return status;
        }
    }

    if (has_current || !log_numbers.empty()) {
        logfile_number_ = versions_.NewFileNumber();
    } else {
        logfile_number_ = 1;
    }

    status = InitWAL();
    if (!status.ok()) {
        return status;
    }

    return RemoveObsoleteFiles();
}

Status DBImpl::RecoverLogFile(uint64_t log_number) {
    const std::string log_name = LogFileName(dbname_, log_number);
    SequentialFile* raw_file = nullptr;
    Status status = options_.env->NewSequentialFile(log_name, &raw_file);
    if (!status.ok()) {
        return status;
    }

    std::unique_ptr<SequentialFile> file(raw_file);
    log::Reader reader(file.get(), true);
    std::string record;
    Status read_status;

    while (reader.ReadRecord(&record, &read_status)) {
        if (record.size() < kWriteBatchHeaderSize) {
            return Status::Corruption("short WriteBatch record in WAL", log_name);
        }

        WriteBatch batch;
        WriteBatchInternal::SetContents(&batch, Slice(record));
        const int count = WriteBatchInternal::Count(&batch);
        if (count < 0) {
            return Status::Corruption("negative WriteBatch count in WAL", log_name);
        }
        if (count == 0) {
            continue;
        }

        const SequenceNumber batch_sequence = WriteBatchInternal::Sequence(&batch);
        const SequenceNumber count_as_sequence = static_cast<SequenceNumber>(count);
        if (batch_sequence > kMaxSequenceNumber - count_as_sequence + 1) {
            return Status::Corruption("WriteBatch sequence overflow in WAL", log_name);
        }

        status = WriteBatchInternal::InsertInto(&batch, mem_.get());
        if (!status.ok()) {
            return status;
        }

        const SequenceNumber batch_last_sequence = batch_sequence + count_as_sequence - 1;
        if (last_sequence_ < batch_last_sequence) {
            last_sequence_ = batch_last_sequence;
        }

        if (mem_->ApproximateMemoryUsage() > options_.write_buffer_size) {
            imm_ = std::move(mem_);
            mem_ = std::make_unique<MemTable>(internal_comparator_);
            status = FlushMemTable();
            if (!status.ok()) {
                return status;
            }
        }
    }

    if (!read_status.ok()) {
        return read_status;
    }

    return Status::OK();
}

Status DBImpl::RemoveObsoleteFiles() {
    std::vector<std::string> filenames;
    Status status = options_.env->GetChildren(dbname_, &filenames);
    if (!status.ok()) {
        return status;
    }

    // 保存当前 Version 仍然引用的 SSTable 文件号
    std::set<uint64_t> live_tables;
    for (int level = 0; level < kNumLevels; ++level) {
        for (int index = 0; index < versions_.current()->NumFiles(level); ++index) {
            const FileMetaData* file = versions_.current()->File(level, index);
            live_tables.insert(file->number);
        }
    }

    // 保存CURRENT 当前指向的 MANIFEST 文件号，0表示没有 MANIFEST
    uint64_t current_manifest_number = 0;
    if (options_.env->FileExists(CurrentFileName(dbname_))) {
        std::string current;
        status = ReadFileToString(options_.env, CurrentFileName(dbname_), &current);
        if (!status.ok()) {
            return status;
        }
        if (!current.empty() && current.back() == '\n') {
            current.pop_back();
        }
        FileType current_type;
        if (!ParseFileName(current, &current_manifest_number, &current_type) || current_type != kDescriptorFile) {
            return Status::Corruption("CURRENT points to invalid MANIFEST", current);
        }
    }

    // 文件名解析判断
    for (const std::string& filename : filenames) {
        if (IsDotOrDotDot(filename)) {
            continue;
        }

        uint64_t number = 0;
        FileType type;
        if (!ParseFileName(filename, &number, &type)) {
            continue;
        }

        bool keep = true;
        switch (type) {
        case kTableFile:
            keep = live_tables.count(number) != 0;
            break;
        case kDescriptorFile:
            keep = number == current_manifest_number;
            break;
        case kLogFile:
            keep = current_manifest_number == 0 || versions_.LogNumber() == 0 || number >= versions_.LogNumber() || number == logfile_number_;
            break;
        case kTempFile:
            keep = false;
            break;
        case kCurrentFile:
        case kDBLockFile:
        case kInfoLogFile:
            keep = true;
            break;
        }
        
        if (!keep) {
            const std::string path = dbname_ + "/" + filename;
            if (options_.env->FileExists(path)) {
                status = options_.env->RemoveFile(path);
            }
        }
    }

    return Status::OK();
}

Status DB::Open(const Options& options, const std::string& dbname, DB** dbptr) {
    if (dbptr == nullptr) {
        return Status::InvalidArgument("DB::Open dbptr is null");
    }

    *dbptr = nullptr;

    if (options.comparator == nullptr) {
        return Status::InvalidArgument("DB::Open comparator is null");
    }

    DBImpl* impl = new DBImpl(options, dbname);

    Status s = impl->Open();
    if (!s.ok()) {
        delete impl;
        return s;
    }

    *dbptr = impl;
    return Status::OK();
}

Status DBImpl::MakeRoomForWrite() {
    if (imm_ != nullptr) {
        return FlushMemTable();
    }

    if (mem_->ApproximateMemoryUsage() <= options_.write_buffer_size) {
        return Status::OK();
    }

    imm_ = std::move(mem_);
    mem_ = std::make_unique<MemTable>(internal_comparator_);
    return FlushMemTable();
}

Status DBImpl::FlushMemTable() {
    if (imm_ == nullptr) {
        return Status::OK();
    }

    FileMetaData meta;
    meta.number = versions_.NewFileNumber();

    std::unique_ptr<Iterator> iter(imm_->NewIterator());
    Status status = BuildTable(dbname_, options_.env, table_options_, iter.get(), &meta);
    if (!status.ok()) {
        return status;
    }

    if (meta.file_size > 0) {
        VersionEdit edit;
        edit.SetLogNumber(logfile_number_);
        edit.SetLastSequence(last_sequence_);
        edit.AddFile(0, meta.number, meta.file_size, meta.smallest, meta.largest);
        status = versions_.LogAndApply(&edit);
        if (!status.ok()) {
            (void)options_.env->RemoveFile(TableFileName(dbname_, meta.number));
            return status;
        }
    }

    imm_.reset();
    return Status::OK();
}

}