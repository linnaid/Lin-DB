#include <lindb/write_batch.h>

#include <cassert>
#include <cstdint>
#include <limits>

#include "Lin-DB/db/memtable.h"
#include "Lin-DB/db/write_batch_internal.h"
#include "Lin-DB/util/coding.h"

namespace lindb {
namespace  {

constexpr size_t kHeaderSize = 12;

constexpr unsigned char kRecordDeletion = 0;
constexpr unsigned char kRecordValue = 1;

void EncodeFixed32(char* dst, uint32_t value) {
    auto* buffer = reinterpret_cast<unsigned char*>(dst);
    buffer[0] = static_cast<unsigned char>(value);
    buffer[1] = static_cast<unsigned char>(value >> 8);
    buffer[2] = static_cast<unsigned char>(value >> 16);
    buffer[3] = static_cast<unsigned char>(value >> 24);
}

uint32_t DecodeFixed32(const char* ptr) {
    const auto* buffer = reinterpret_cast<const unsigned char*>(ptr);
    return static_cast<uint32_t>(buffer[0]) |
            (static_cast<uint32_t>(buffer[1]) << 8) |
            (static_cast<uint32_t>(buffer[2]) << 16) |
            (static_cast<uint32_t>(buffer[3]) << 24);
}

void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
    assert(value.size() <= std::numeric_limits<uint32_t>::max());

    PutVarint32(dst, static_cast<uint32_t>(value.size()));
    dst->append(value.data(), value.size());
}

bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
    uint32_t length = 0;
    const char* begin = input->data();
    const char* limit = begin + input->size();
    const char* data = GetVarint32Ptr(begin, limit, &length);

    if (data == nullptr || static_cast<size_t>(limit - data) < length) {
        return false;
    }

    *result = Slice(data, length);
    input->remove_prefix(static_cast<size_t>(data - begin) + length);
    return true;
}

class MemTableInserter : public WriteBatch::Handler {
public:
    MemTableInserter(SequenceNumber seq, MemTable* memtable) 
        : sequence_(seq), memtable_(memtable) {}

    void Put(const Slice& key, const Slice& value) override {
        memtable_->Add(sequence_, kTypeValue, key, value);
        ++sequence_;
    }

    void Delete(const Slice& key) override {
        memtable_->Add(sequence_, kTypeDeletion, key, Slice());
        ++sequence_;
    }

private:
    SequenceNumber sequence_;
    MemTable* memtable_;
};

}

WriteBatch::WriteBatch() {
    Clear();
}

void WriteBatch::Put(const Slice& key, const Slice& value) {
    WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
    rep_.push_back(static_cast<char>(kRecordValue));
    PutLengthPrefixedSlice(&rep_, key);
    PutLengthPrefixedSlice(&rep_, value);
}

void WriteBatch::Delete(const Slice& key) {
    WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
    rep_.push_back(static_cast<char>(kRecordDeletion));
    PutLengthPrefixedSlice(&rep_, key);
}

void WriteBatch::Clear() {
    rep_.clear();
    rep_.resize(kHeaderSize);
    WriteBatchInternal::SetSequence(this, 0);
    WriteBatchInternal::SetCount(this, 0);
}

void WriteBatch::Append(const WriteBatch& source) {
    WriteBatchInternal::Append(this, &source);
}

Status WriteBatch::Iterate(Handler* handler) const {
    assert(handler != nullptr);

    if (rep_.size() < kHeaderSize) {
        return Status::Corruption("malformed WriteBatch header");
    }

    Slice input(rep_.data() + kHeaderSize, rep_.size() - kHeaderSize);
    int found = 0;

    while (!input.empty()) {
        const unsigned char tag = static_cast<unsigned char>(input.data()[0]);
        input.remove_prefix(1);

        switch (tag)
        {
        case kRecordValue: {
            Slice key;
            Slice value;
            if (!GetLengthPrefixedSlice(&input, &key) ||
                !GetLengthPrefixedSlice(&input, &value)) {
                    return Status::Corruption("bad WriteBatch Put record");
                }
            handler->Put(key, value);
            ++found;
            break;
        }
        case kRecordDeletion: {
            Slice key;
            if (!GetLengthPrefixedSlice(&input, &key)) {
                return Status::Corruption("bad WriteBatch Delete record");
            }
            handler->Delete(key);
            ++found;
            break;
        }        
        default:
            return Status::Corruption("unknown WriteBatch record tag");
        }
    }

    if (found != WriteBatchInternal::Count(this)) {
        return Status::Corruption("WriteBatch count mismatch");
    }

    return Status::OK();
}

SequenceNumber WriteBatchInternal::Sequence(const WriteBatch* batch) {
    assert(batch->rep_.size() >= kHeaderSize);
    return DecodeFixed64(batch->rep_.data());
}

void WriteBatchInternal::SetSequence(WriteBatch* batch, SequenceNumber seq) {
    assert(batch->rep_.size() >= kHeaderSize);

    EncodeFixed64(batch->rep_.data(), seq);
}

int WriteBatchInternal::Count(const WriteBatch* batch) {
    assert(batch->rep_.size() >= kHeaderSize);

    return static_cast<int>(DecodeFixed32(batch->rep_.data() + 8));
}

void WriteBatchInternal::SetCount(WriteBatch* batch, int count) {
    assert(batch->rep_.size() >= kHeaderSize);
    assert(count >= 0);

    EncodeFixed32(&batch->rep_[8], static_cast<uint32_t>(count));
}

Slice WriteBatchInternal::Contents(const WriteBatch* batch) {
    return Slice(batch->rep_);
}

size_t WriteBatchInternal::ByteSize(const WriteBatch* batch) {
    return batch->rep_.size();
}

void WriteBatchInternal::SetContents(WriteBatch* batch, const Slice& contents) {
    batch->rep_.assign(contents.data(), contents.size());
}

Status WriteBatchInternal::InsertInto(const WriteBatch* batch, MemTable* memtable) {
    assert(batch != nullptr);

    if (batch->rep_.size() < kHeaderSize) {
        return Status::Corruption("malformed WriteBatch header");
    }

    MemTableInserter inserter(Sequence(batch), memtable);
    return batch->Iterate(&inserter);
}

void WriteBatchInternal::Append(WriteBatch* dst, const WriteBatch* src) {
    SetCount(dst, Count(dst) + Count(src));

    assert(src->rep_.size() >= kHeaderSize);
    dst->rep_.append(src->rep_.data() + kHeaderSize, src->rep_.size() - kHeaderSize);
}

}