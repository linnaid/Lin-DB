#include <lindb/iterator.h>

#include <utility>

namespace lindb {

namespace {

// 一个永远为空的迭代器实现
class EmptyIterator : public Iterator {
public:
    explicit EmptyIterator(Status status)
        : status_(std::move(status)) {}

    bool Valid() const override { return false; }
    void SeekToFirst() override {}
    void SeekToLast() override{} 
    void Seek(const Slice& target) override { (void)target; }

    void Next() override {
        assert(false);
    }
    void Prev() override {
        assert(false);
    }

    Slice key() const override {
        assert(false);
        return Slice();
    }

    Slice value() const override {
        assert(false);
        return Slice();
    }

    Status status() const override { return status_; }

private:
    Status status_;
};

}

Iterator::Iterator() {
    cleanup_head_.function = nullptr;
    cleanup_head_.arg1 = nullptr;
    cleanup_head_.arg2 = nullptr;
    cleanup_head_.next = nullptr;
}

Iterator::~Iterator() {
    if (cleanup_head_.IsEmpty()) {
        return;
    } 

    CleanupNode* node = &cleanup_head_;
    while (node != nullptr) {
        node->Run();
        CleanupNode* next = node->next;
        if (node != &cleanup_head_) {
            delete node;
        }
        node = next;
    }
}

void Iterator::RegisterCleanup(CleanupFunction function, void* arg1, void* arg2) {
    assert(function != nullptr);

    if (cleanup_head_.IsEmpty()) {
        cleanup_head_.function = function;
        cleanup_head_.arg1 = arg1;
        cleanup_head_.arg2 = arg2;
        cleanup_head_.next = nullptr;
        return;
    }

    CleanupNode* node = new CleanupNode;
    node->function = function;
    node->arg1 = arg1;
    node->arg2 = arg2;
    node->next = cleanup_head_.next;
    cleanup_head_.next = node;
}

Iterator* NewEmptyIterator() {
    return new EmptyIterator(Status::OK());
}

Iterator* NewErrorIterator(const Status& status) {
    return new EmptyIterator(status);
}

}
