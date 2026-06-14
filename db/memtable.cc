#include "Lin-DB/db/memtable.h"

#include <cstdint>
#include <cstring>
#include <cassert>
#include <limits>

#include <lindb/iterator.h>

#include "Lin-DB/util/coding.h"

namespace lindb {

namespace {

// 无 limit 的解析 varint
const char* DecodeEntryVarint32(const char* p, uint32_t* value) {
    uint32_t result = 0;

    // 每 7 bit 存一组，最多 5 组
    for (uint32_t shift = 0; shift <= 28; shift += 7) {
        const uint32_t byte = static_cast<unsigned char>(*p);

        ++p;

        if ((byte & 128u) != 0) {
            result |= ((byte & 127u) << shift);
        } else {
            result |= (byte << shift);
            *value = result;
            
            return p;
        }
    }

    assert(false);
    return nullptr;
}

// 读 varint 解析出来长度的 bytes
Slice GetLengthPrefixedSlice(const char* data) {
    uint32_t len = 0;

    const char* p = DecodeEntryVarint32(data, &len);

    assert(p != nullptr);

    return Slice(p, len);
}

}

class MemTable::MemTableIterator : public Iterator {
public:
    explicit MemTableIterator(const Table* table)
        : iter_(table) {}

    bool Valid() const override { return iter_.Valid(); }

    void SeekToFirst() override { iter_.SeekToFirst(); }

    void SeekToLast() override { iter_.SeekToLast(); }

    void Seek(const Slice& target) override { 
        assert(target.size() <= std::numeric_limits<uint32_t>::max());

        std::string memtable_key;
        PutVarint32(&memtable_key, static_cast<uint32_t>(target.size()));
        memtable_key.append(target.data(), target.size());

        iter_.Seek(memtable_key.data());
    }

    void Next() override {
        assert(Valid());
        iter_.Next();
    }

    void Prev() override {
        assert(Valid());
        iter_.Prev();
    }

    Slice key() const override {
        assert(Valid());
        return GetLengthPrefixedSlice(iter_.key());
    }

    Slice value() const override {
        assert(Valid());

        const char* entry = iter_.key();
        Slice entry_key = GetLengthPrefixedSlice(entry);
        const char* value_ptr = entry_key.data() + entry_key.size();

        return GetLengthPrefixedSlice(value_ptr);
    }

    Status status() const override { return Status::OK(); }

private:
    Table::Iterator iter_;
};

MemTable::KeyComparator::KeyComparator(const InternalKeyComparator& c)
    : comparator(c) {}

int MemTable::KeyComparator::operator()(const char* a, const char* b) const {
    Slice a_key = GetLengthPrefixedSlice(a);
    Slice b_key = GetLengthPrefixedSlice(b);

    return comparator.Compare(a_key, b_key);
}

MemTable::MemTable(const InternalKeyComparator& comparator) 
    : comparator_(comparator),
      table_(comparator_, &arena_) {}

void MemTable::Add(SequenceNumber seq, 
    ValueType type, const Slice& key, const Slice& value) {
        const size_t key_size = key.size();
        const size_t value_size = value.size();
        const size_t internal_key_size = key_size + 8;

        assert(internal_key_size <= std::numeric_limits<uint32_t>::max());
        assert(value_size <= std::numeric_limits<uint32_t>::max());

        const size_t encoded_len = VarintLength(internal_key_size) + internal_key_size + VarintLength(value_size) + value_size;

        char* buf = arena_.Allocate(encoded_len);
        char* p = buf;

        p = EncodeVarint32(p, static_cast<uint32_t>(internal_key_size));
        std::memcpy(p, key.data(), key_size);
        p += key_size;
        EncodeFixed64(p, PackSequenceAndType(seq, type));
        p += 8;
        p = EncodeVarint32(p, value_size);
        std::memcpy(p, value.data(), value_size);
        p += value_size;
        assert(p == buf + encoded_len);

        table_.Insert(buf);
    }

bool MemTable::Get(const LookupKey& key, std::string* value, Status* status) {
    assert(value != nullptr);
    assert(status != nullptr);

    Slice memtable_key = key.memtable_key();
    Table::Iterator iter(&table_);
    iter.Seek(memtable_key.data());

    if (iter.Valid() == false) {
        *status = Status::NotFound(key.user_key());
        return false;
    }

    const char* entry = iter.key();
    Slice entry_key = GetLengthPrefixedSlice(entry);
    Slice entry_user_key = ExtractUserKey(entry_key);

    if (comparator_.comparator.user_comparator()->Compare(entry_user_key, key.user_key()) != 0) {
        *status = Status::NotFound(key.user_key());
        return false;
    }

    const uint64_t tag = DecodeFixed64(entry_key.data() + entry_key.size() - 8);
    const ValueType type = static_cast<ValueType>(tag & 0xff);

    if (type == kTypeValue) {
        const char* value_ptr = entry_key.data() + entry_key.size();
        Slice v = GetLengthPrefixedSlice(value_ptr);
        value->assign(v.data(), v.size());
        *status = Status::OK();
        return true;
    }

    if (type == kTypeDeletion) {
        value->clear();
        *status = Status::NotFound(key.user_key());
        return true;
    }

    value->clear();
    *status = Status::Corruption("bad value type for", key.user_key());
    return true;
}

bool MemTable::Get(const LookupKey& key, std::string* value) {
    Status status;
    const bool found = Get(key, value, &status);
    return found && status.ok();
}

Iterator* MemTable::NewIterator() const {
    return new MemTableIterator(&table_);
}

size_t MemTable::ApproximateMemoryUsage() const {
    return arena_.MemoryUsage();
}

}