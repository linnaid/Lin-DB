#include <db/dbformat.h>

#include <cassert>
#include <cstring>
#include <limits>

#include "Lin-DB/util/coding.h"


namespace lindb {
    ParsedInternalKey::ParsedInternalKey()
        :user_key(), sequence(0), type(kTypeDeletion) {}

    ParsedInternalKey::ParsedInternalKey(const Slice& u, 
                                         SequenceNumber seq, 
                                         ValueType t)
        : user_key(u), sequence(seq), type(t) {}

    uint64_t PackSequenceAndType(SequenceNumber seq, ValueType type) {
        assert(seq <= kMaxSequenceNumber);
        assert(type <= kValueTypeForSeek);
        return (seq << 8) | type;
    }

    Slice ExtractUserKey(const Slice& internal_key) {
        assert(internal_key.size() >= 8);
        return Slice(internal_key.data(), internal_key.size() - 8);
    }

    size_t InternalKeyEncodingLength(const ParsedInternalKey& key) {
        return key.user_key.size() + 8;
    }

    void AppendInternalKey(std::string* result, const ParsedInternalKey& key) {
        result->append(key.user_key.data(), key.user_key.size());
        PutFixed64(result, PackSequenceAndType(key.sequence, key.type));
    }

    bool ParseInternalKey(const Slice& internal_key, ParsedInternalKey* result) {
        const size_t n = internal_key.size();
        if (n < 8) {
            return false;
        }

        const uint64_t num = DecodeFixed64(internal_key.data() + n - 8);
        const uint8_t type = static_cast<uint8_t>(num & 0xff);
        if (type > static_cast<uint8_t>(kTypeValue)) {
            return false;
        }

        result->user_key = Slice(internal_key.data(), n - 8);
        result->sequence = num >> 8;
        result->type = static_cast<ValueType>(type);
        return true;
    }

    InternalKey::InternalKey() = default;

    InternalKey::InternalKey(const Slice& user_key, 
                             SequenceNumber seq, 
                             ValueType type) {
        AppendInternalKey(&rep_, ParsedInternalKey(user_key, seq, type));
    }

    Slice InternalKey::Encode() const {
        assert(!rep_.empty());
        return Slice(rep_);
    }

    bool InternalKey::DecodeFrom(const Slice& encoded) {
        rep_.assign(encoded.data(), encoded.size());
        return !rep_.empty();
    }

    Slice InternalKey::user_key() const {
        return ExtractUserKey(Encode());
    }

    void InternalKey::SetFrom(const ParsedInternalKey& key) {
        rep_.clear();
        AppendInternalKey(&rep_, key);
    }

    void InternalKey::Clear() {
        rep_.clear();
    }

    InternalKeyComparator::InternalKeyComparator(
        const Comparator* user_comparator)
        :user_comparator_(user_comparator) {}

    int InternalKeyComparator::Compare(const Slice& a, const Slice& b) const {
        int result = user_comparator_->Compare(ExtractUserKey(a), ExtractUserKey(b));
        if (result == 0) {
            const uint64_t a_num = DecodeFixed64(a.data() + a.size() - 8);
            const uint64_t b_num = DecodeFixed64(b.data() + b.size() - 8);
            if (a_num > b_num) {
                return -1;
            } else if (a_num < b_num) {
                return 1;
            }
        }
        return result;
    }

    const char* InternalKeyComparator::Name() const {
        return "lindb.InternalKeyComparator";
    }

    const Comparator* InternalKeyComparator::user_comparator() const {
        return user_comparator_;
    }

    int InternalKeyComparator::Compare(const InternalKey& a, 
                                       const InternalKey& b) const {
        return Compare(a.Encode(), b.Encode());
    }

    LookupKey::LookupKey(const Slice& user_key, SequenceNumber sequence) {
        const size_t usize = user_key.size();
        assert(usize <= static_cast<size_t>(std::numeric_limits<uint32_t>::max() - 8));

        const size_t needed = usize + 13;
        char* dst = (needed <= sizeof(space_)) ? space_ : new char[needed];
        start_ = dst;

        dst = EncodeVarint32(dst, static_cast<uint32_t>(usize + 8));
        kstart_ = dst;

        std::memcpy(dst, user_key.data(), usize);
        dst += usize;

        EncodeFixed64(dst, PackSequenceAndType(sequence, kValueTypeForSeek));
        dst += 8;
        end_ = dst;
    }

    LookupKey::~LookupKey() {
        if (start_ != space_) {
            delete[] start_;
        }
    }

    Slice LookupKey::memtable_key() const {
        return Slice(start_, static_cast<size_t>(end_ - start_));
    }

    Slice LookupKey::internal_key() const {
        return Slice(kstart_, static_cast<size_t>(end_ - kstart_));
    }

    Slice LookupKey::user_key() const {
        return Slice(kstart_, static_cast<size_t>(end_ - kstart_ - 8));
    }
    
}