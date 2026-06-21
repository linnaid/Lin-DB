#include "Lin-DB/db/db_iter.h"

#include <cassert>
#include <memory>
#include <string>

#include <lindb/comparator.h>
#include <lindb/status.h>

namespace lindb {
namespace {

class DBIter : public Iterator {
public:
    // 记录当前是正/反向遍历
    enum Direction {
        kForward,
        kReverse, 
    };

    DBIter(const Comparator* user_comparator, Iterator* internal_iter, SequenceNumber sequence)
        : user_comparator_(user_comparator), 
          iter_(internal_iter), 
          sequence_(sequence), 
          direction_(kForward), 
          valid_(false) {
        assert(user_comparator_ != nullptr);
        assert(iter_ != nullptr);
    }

    ~DBIter() override = default;

    bool Valid() const override {
        return valid_;
    }

    Slice key() const override {
        assert(valid_);
        return (direction_ == kForward) ? ExtractUserKey(iter_->key()) : Slice(saved_key_);
    }

    Slice value() const override {
        assert(valid_);
        return (direction_ == kForward) ? iter_->value() : Slice(saved_value_);
    }

    Status status() const override {
        if (status_.ok()) {
            return iter_->status();
        }
        return status_;
    }

    void Next() override {
        assert(valid_);
        if (direction_ == kReverse) {
            direction_ = kForward;
            if (!iter_->Valid()) {
                iter_->SeekToFirst();
            } else {
                iter_->Next();
            }
            if (!iter_->Valid()) {
                valid_ = false;
                saved_key_.clear();
                return;
            }
        }
        std::string* skip = &saved_key_;
        SaveKey(ExtractUserKey(iter_->key()), skip);
        FindNextUserEntry(true, skip);
    }

    void Prev() override {
        assert(valid_);
        if (direction_ == kForward) {
            assert(iter_->Valid());
            SaveKey(ExtractUserKey(iter_->key()), &saved_key_);

            while (true) {
                iter_->Prev();
                if (!iter_->Valid()) {
                    valid_ = false;
                    saved_key_.clear();
                    ClearSavedValue();
                    return;
                }
                if (user_comparator_->Compare(ExtractUserKey(iter_->key()), saved_key_) < 0) {
                    break;
                }
            }
            direction_ = kReverse;
        }
        FindPrevUserEntry();
    }

    void Seek(const Slice& target) override {
        direction_ = kForward;
        ClearSavedValue();
        saved_key_.clear();
        AppendInternalKey(&saved_key_, ParsedInternalKey(target, sequence_, kValueTypeForSeek));
        iter_->Seek(saved_key_);
        if (iter_->Valid()) {
            FindNextUserEntry(false, &saved_key_);
        } else {
            valid_ = false;
        }
    }

    void SeekToFirst() override {
        direction_ = kForward;
        ClearSavedValue();
        iter_->SeekToFirst();
        if (iter_->Valid()) {
            FindNextUserEntry(false, &saved_key_);
        } else {
            valid_ = false;
        }
    }

    void SeekToLast() override {
        direction_ = kReverse;
        ClearSavedValue();
        iter_->SeekToLast();
        FindPrevUserEntry(); 
    }

private:
    void SaveKey(const Slice& key, std::string* dst) {
        dst->assign(key.data(), key.size());
    }

    void ClearSavedValue() {
        if (saved_value_.capacity() > 1048576) {
            std::string empty;
            saved_value_.swap(empty);
        } else {
            saved_value_.clear();
        }
    }

    // 解析当前 internal key，并在失败时记录 Corruption
    bool ParseKey(ParsedInternalKey* key) {
        Slice internal_key = iter_->key();
        if (!ParseInternalKey(internal_key, key)) {
            status_ = Status::Corruption("corrupted internal key in DBIter");
            return false;
        }
        return true;
    }

    void FindNextUserEntry(bool skipping, std::string* skip) {
        assert(iter_->Valid());
        assert(direction_ == kForward);
        do {
            ParsedInternalKey ikey;
            if (ParseKey(&ikey) && ikey.sequence <= sequence_) {
                switch (ikey.type) {
                    case kTypeDeletion:
                        SaveKey(ikey.user_key, skip);
                        skipping = true;
                        break;
                    case kTypeValue:
                        if (skipping && user_comparator_->Compare(ikey.user_key, *skip) <= 0) {

                        } else {
                            valid_ = true;
                            saved_key_.clear();
                            return;
                        }
                        break;
                    default:
                        status_ = Status::Corruption("unknown value type in DBIter");
                        return;
                }
            }
            iter_->Next();
        } while (iter_->Valid());
        saved_key_.clear();
        valid_ = false;
    }

    void FindPrevUserEntry() {
        assert(direction_ == kReverse);
        ValueType value_type = kTypeDeletion;
        if (iter_->Valid()) {
            do {
                ParsedInternalKey ikey;
                if (ParseKey(&ikey) && ikey.sequence <= sequence_) {
                    if ((value_type != kTypeDeletion) && 
                         user_comparator_->Compare(ikey.user_key, saved_key_) < 0) {
                        // 已经找到一个有效 user key(也就是当前要扫描的key已经结束)
                        break;
                    }
                    value_type = ikey.type;

                    if (value_type == kTypeDeletion) {
                        saved_key_.clear();
                        ClearSavedValue();
                    } else {
                        Slice raw_value = iter_->value();
                        if (saved_value_.capacity() > raw_value.size() + 1048576) {
                            std::string empty;
                            saved_value_.swap(empty);
                        }
                        SaveKey(ExtractUserKey(iter_->key()), &saved_key_);
                        saved_value_.assign(raw_value.data(), raw_value.size());
                    }
                } 
                iter_->Prev();
            } while (iter_->Valid());
        }
        if (value_type == kTypeDeletion) {
            valid_ = false;
            saved_key_.clear();
            ClearSavedValue();
            direction_ = kForward;
        } else {
            valid_ = true;
        }
    }

    const Comparator* user_comparator_;
    std::unique_ptr<Iterator> iter_;
    SequenceNumber sequence_;
    Status status_;
    std::string saved_key_;
    std::string saved_value_;
    Direction direction_;
    bool valid_;
};

}

Iterator* NewDBIterator(const Comparator* user_comparator, Iterator* internal_iter, SequenceNumber sequence) {
    return new DBIter(user_comparator, internal_iter, sequence);
}

}