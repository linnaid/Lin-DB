#include  "Lin-DB/db/db_impl.h"

#include <cassert>
#include <utility>


namespace lindb {

DBImpl::DBImpl(const Options& options, std::string dbname)
    : options_(options),
      dbname_(std::move(dbname)),
      internal_comparator_(options_.comparator),
      mem_(internal_comparator_),
      last_sequence_(0) {}

Status DBImpl::Put(const WriteOptions& options, const Slice& key, const Slice& value) {
    (void)options;

    const SequenceNumber sequence = NewSequence();
    mem_.Add(sequence, kTypeValue, key, value);
    return Status::OK();
}

Status DBImpl::Delete(const WriteOptions& options, const Slice& key) {
    (void)options;

    const SequenceNumber sequence = NewSequence();
    mem_.Add(sequence, kTypeDeletion, key, Slice());
    return Status::OK();
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