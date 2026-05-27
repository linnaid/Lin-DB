// 包装普通 Iterator, 缓存 Valid/key 结果，减少虚函数调用，并负责释放内部 iterator
#pragma once

#include <cassert>

#include <lindb/iterator.h>

namespace lindb {

class IteratorWrapper {
public:
    IteratorWrapper() : iter_(nullptr), valid_(false) {}

    explicit IteratorWrapper(Iterator* iter) 
        : iter_(nullptr), valid_(false) {
            Set(iter);
        }
    ~IteratorWrapper() {
        delete iter_;
    }

    Iterator* iter() const { return iter_; }

    void Set(Iterator* iter) {
        delete iter_;
        iter_ = iter;
        Update();
    }

    bool Valid() const { return valid_; }

    Slice key() const {
        assert(Valid());
        return key_;
    }

    Slice value() const {
        assert(Valid());
        return iter_->value();
    }

    Status status() const {
        return iter_ == nullptr ? Status::OK() : iter_->status();
    }

    void SeekToFirst() {
        assert(iter_ != nullptr);
        iter_->SeekToFirst();
        Update();
    }

    void SeekToLast() {
        assert(iter_ != nullptr);
        iter_->SeekToLast();
        Update();
    }

    void Seek(const Slice& target) {
        assert(iter_ != nullptr);
        iter_->Seek(target);
        Update();
    }

    void Next() {
        assert(iter_ != nullptr && Valid());
        iter_->Next();
        Update();
    }

    void Prev() {
        assert(iter_ != nullptr && Valid());
        iter_->Prev();
        Update();
    }

private:
    void Update() {
        valid_ = iter_ != nullptr && iter_->Valid();
        if (valid_) {
            key_ = iter_->key();
        }
    }

    Iterator* iter_;
    bool valid_;
    Slice key_;
};

}