#include  "Lin-DB/db/db_impl.h"

#include <cassert>
#include <utility>

#include <lindb/write_batch.h>
#include "Lin-DB/db/write_batch_internal.h"


namespace lindb {

DBImpl::DBImpl(const Options& options, std::string dbname)
    : options_(options),
      dbname_(std::move(dbname)),
      internal_comparator_(options_.comparator),
      mem_(internal_comparator_),
      last_sequence_(0) {}

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
    batch.Put(key, Slice());
    return Write(options, &batch);
}

Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
    (void)options;

    if (updates == nullptr) {
        return Status::InvalidArgument("DBImpl::Write updates is null");
    }

    const int count = WriteBatchInternal::Count(updates);
    if (count == 0) {
        return Status::OK();
    }

    assert(last_sequence_ <= kMaxSequenceNumber - static_cast<SequenceNumber>(count));
    const SequenceNumber frist_sequence = last_sequence_ + 1;
    WriteBatchInternal::SetSequence(updates, frist_sequence);

    Status s = WriteBatchInternal::InsertInto(updates, &mem_);
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
    if(mem_.Get(lookup_key, value)) {
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

Status DB::Open(const Options& options, const std::string& dbname, DB** dbptr) {
    if (dbptr == nullptr) {
        return Status::InvalidArgument("DB::Open dbptr is null");
    }

    *dbptr = nullptr;

    if (options.comparator == nullptr) {
        return Status::InvalidArgument("DB::Open comparator is null");
    }

    *dbptr = new DBImpl(options, dbname);
    return Status::OK();
}

}