#include  "Lin-DB/db/db_impl.h"

#include <cassert>
#include <utility>

#include <lindb/write_batch.h>
#include <lindb/env.h>
#include <lindb/filename.h>
#include <lindb/iterator.h>

#include "Lin-DB/db/write_batch_internal.h"
#include "Lin-DB/db/table_cache.h"
#include "Lin-DB/db/version_set.h"
#include "Lin-DB/db/builder.h"
#include "Lin-DB/db/version_edit.h"

namespace lindb {
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
    (void)options;

    if (value == nullptr) {
        return Status::InvalidArgument("DBImpl::Get value is null");
    }

    LookupKey lookup_key(key, last_sequence_);
    if(mem_->Get(lookup_key, value)) {
        return Status::OK();
    }

    value->clear();
    return Status::NotFound("key not found", key);
}

SequenceNumber DBImpl::NewSequence() {
    assert(last_sequence_ < kMaxSequenceNumber);
    ++last_sequence_;
    return last_sequence_;
}

Status DBImpl::Open() {
    return InitWAL();
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