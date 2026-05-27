#include "Lin-DB/db/two_level_iterator.h"

#include <cassert>
#include <string>

#include <lindb/options.h>
#include "Lin-DB/db/iterator_wrapper.h"

namespace lindb {

namespace {

class TwoLevelIterator : public Iterator {
public:
    TwoLevelIterator(Iterator* index_iter, BlockFunction block_function, 
        void* arg, const ReadOptions& options)
        : block_function_(block_function), arg_(arg), options_(options), 
          index_iter_(index_iter), data_iter_(nullptr) {}

    ~TwoLevelIterator() override = default;

    bool Valid() const override {
        return data_iter_.Valid();
    }

    Slice key() const override {
        assert(Valid());
        return data_iter_.key();
    }

    Slice value() const override {
        assert(Valid());
        return data_iter_.value();
    }

    Status status() const override {
        if (!index_iter_.status().ok()) {
            return index_iter_.status();
        }
        if (data_iter_.iter() != nullptr && !data_iter_.status().ok()) {
            return  data_iter_.status();
        }
        return status_;
    }

    void SeekToFirst() override {
        index_iter_.SeekToFirst();
        InitDataBlock();
        if (data_iter_.iter() != nullptr) {
            data_iter_.SeekToFirst();
        }
        SkipEmptyDataBlocksForward();
    }

    void SeekToLast() override {
        index_iter_.SeekToLast();
        InitDataBlock();
        if (data_iter_.iter() != nullptr) {
            data_iter_.SeekToLast();
        }
        SkipEmptyDataBlocksBackward();
    }

    void Seek(const Slice& target) override {
        index_iter_.Seek(target);
        InitDataBlock();
        if (data_iter_.iter() != nullptr) {
            data_iter_.Seek(target);
        }
        SkipEmptyDataBlocksForward();
    }

    void Next() override {
        assert(Valid());
        data_iter_.Next();
        SkipEmptyDataBlocksForward();
    }

    void Prev() override {
        assert(Valid());
        data_iter_.Prev();
        SkipEmptyDataBlocksBackward();
    }

private:
    void SaveError(const Status& status) {
        if (status_.ok() && !status.ok()) {
            status_ = status;
        }
    }

    void SetDataIterator(Iterator* data_iter) {
        if (data_iter_.iter() != nullptr) {
            SaveError(data_iter_.status());
        }
        data_iter_.Set(data_iter);
    }

    void InitDataBlock() {
        if (!index_iter_.Valid()) {
            SetDataIterator(nullptr);
            return;
        }

        Slice handle = index_iter_.value();
        if (data_iter_.iter() != nullptr && handle.compare(Slice(data_block_handle_)) == 0) {
            return;
        }

        Iterator* iter = (*block_function_)(arg_, options_, handle);
        data_block_handle_.assign(handle.data(), handle.size());
        SetDataIterator(iter);
    }

    void SkipEmptyDataBlocksForward() {
        while (data_iter_.iter() == nullptr || !data_iter_.Valid()) {
            if (!index_iter_.Valid()) {
                SetDataIterator(nullptr);
                return;
            }
            index_iter_.Next();
            InitDataBlock();
            if (data_iter_.iter() != nullptr) {
                data_iter_.SeekToFirst();
            }
        }
    }

    void SkipEmptyDataBlocksBackward() {
        while (data_iter_.iter() == nullptr || !data_iter_.Valid()) {
            if (!index_iter_.Valid()) {
                SetDataIterator(nullptr);
                return;
            }
            index_iter_.Prev();
            InitDataBlock();
            if (data_iter_.iter() != nullptr) {
                data_iter_.SeekToLast();
            }
        }
    }

    BlockFunction block_function_;
    void* arg_;
    ReadOptions options_;
    Status status_;
    IteratorWrapper index_iter_;
    IteratorWrapper data_iter_;
    std::string data_block_handle_;
};

}

Iterator* NewTwoLevelIterator(Iterator* index_iter, BlockFunction block_function, 
    void* arg, const ReadOptions& options) {
        return new TwoLevelIterator(index_iter, block_function, arg, options);
    }

}