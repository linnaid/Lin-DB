#include "Lin-DB/db/merger.h"

#include <cassert>
#include <utility>
#include <vector>

#include <lindb/comparator.h>
#include <lindb/status.h>

#include "Lin-DB/db/iterator_wrapper.h"

namespace lindb {
namespace {

// 把多个已排序 Iterator 合并成一个有序 Iterator
class MergingIterator : public Iterator {
public:
    MergingIterator(const Comparator* comparator, std::vector<Iterator*> children)
        : comparator_(comparator), 
          current_(nullptr),
          direction_(kForward) {
        assert(comparator_ != nullptr);
        children_.reserve(children.size());
        for (Iterator* child : children) {
            children_.emplace_back(child);
        }
    }

    bool Valid() const override {
        return current_ != nullptr;
    }

    // 定位到所有 children 最小 key
    void SeekToFirst() override {
        for (IteratorWrapper& child : children_) {
            child.SeekToFirst();
        }
        direction_ = kForward;
        FindSmallest();
    }

    void SeekToLast() override {
        for (IteratorWrapper& child : children_) {
            child.SeekToLast();
        }
        direction_ = kReverse;
        FindLargest();
    }

    void Seek(const Slice& target) override {
        for (IteratorWrapper& child : children_) {
            child.Seek(target);
        }
        direction_ = kForward;
        FindSmallest();
    }

    void Next() override {
        assert(Valid());
        if (direction_ != kForward) {
            for (IteratorWrapper& child : children_) {
                if (&child != current_) {
                    child.Seek(key());
                    if (child.Valid() && comparator_->Compare(key(), child.key()) == 0) {
                        child.Next();
                    }
                }
            }
            direction_ = kForward;
        }
        current_->Next();
        FindSmallest();
    }

    void Prev() override {
        assert(Valid());
        if (direction_ != kReverse) {
            for (IteratorWrapper& child : children_) {
                if (&child != current_) {
                    child.Seek(key());
                    if (child.Valid()) {
                        child.Prev();
                    } else {
                        child.SeekToLast();
                    }
                }
            }
            direction_ = kReverse;
        }
        current_->Prev();
        FindLargest();
    }

    Slice key() const override {
        assert(Valid());
        return current_->key();
    }

    Slice value() const override {
        assert(Valid());
        return current_->value();
    }

    Status status() const override {
        for (const IteratorWrapper& child : children_) {
            Status status = child.status();
            if (!status.ok()) {
                return status;
            }
        }
        return Status::OK();
    }

private:
    enum Direction {
        kForward, 
        kReverse, 
    };

    void FindSmallest() {
        IteratorWrapper* smallest = nullptr;
        for (IteratorWrapper& child : children_) {
            if (child.Valid()) {
                if (smallest == nullptr || comparator_->Compare(child.key(), smallest->key()) < 0) {
                    smallest = &child;
                }
            }
        }
        current_ = smallest;
    }

    void FindLargest() {
        IteratorWrapper* largest = nullptr;
        for (IteratorWrapper& child : children_) {
            if (child.Valid()) {
                if (largest == nullptr || comparator_->Compare(child.key(), largest->key()) > 0) {
                    largest = &child;
                }
            }
        }
        current_ = largest;
    }

    const Comparator* comparator_;
    std::vector<IteratorWrapper> children_;
    // 当前选中的 child
    IteratorWrapper* current_;
    Direction direction_;
};

}

Iterator* NewMergingIterator(const Comparator* comparator, std::vector<Iterator*> children) {
    if (children.empty()) {
        return NewEmptyIterator();
    }
    if (children.size() == 1) {
        return children[0];
    }
    return new MergingIterator(comparator, std::move(children));
}

}