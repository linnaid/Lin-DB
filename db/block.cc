#include <lindb/block.h>
#include <lindb/comparator.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

#include "Lin-DB/util/coding.h"
#include "Lin-DB/util/logging.h"

namespace lindb {

namespace {

const char* DecodeEntry(const char* p, const char* limit, uint32_t* shared, uint32_t* non_shared, uint32_t* value_length) {
    if (limit - p < 3) {
        return nullptr;
    }

    *shared = static_cast<uint8_t>(p[0]);
    *non_shared = static_cast<uint8_t>(p[1]);
    *value_length = static_cast<uint8_t>(p[2]);

    if (((*shared) | (*non_shared) | (*value_length)) < 128) {
        p += 3;
    } else {
        if ((p = GetVarint32Ptr(p, limit, shared)) == nullptr) {
            return nullptr;
        }
        if ((p = GetVarint32Ptr(p, limit, non_shared)) == nullptr) {
            return nullptr;
        }
        if ((p = GetVarint32Ptr(p, limit, value_length)) == nullptr) {
            return nullptr;
        }
    }

    if (static_cast<uint32_t>(limit - p) < (*non_shared + *value_length)) {
        return nullptr;
    }

    return p;
}

class BlockIter : public Iterator {
public:
    BlockIter(const Comparator* comparator, const char* data, uint32_t restarts, uint32_t num_restarts)
        : comparator_(comparator), data_(data), restarts_(restarts), num_restarts_(num_restarts), current_(restarts), restart_index_(num_restarts) {
            assert(num_restarts > 0);
        }

    bool Valid() const override {
        return current_ < restarts_;
    }

    void SeekToFirst() override {
        SeekToRestartPoint(0);
        ParseNextKey();
    }

    void SeekToLast() override {
        SeekToRestartPoint(num_restarts_ - 1);
        while (ParseNextKey() && NextEntryOffset() < restarts_) {
        }
    }

    void Seek(const Slice& target) override {
        uint32_t left = 0;
        uint32_t right = num_restarts_ - 1;
        int current_key_compare = 0;

        if (Valid()) {
            current_key_compare = comparator_->Compare(key_, target);
            if (current_key_compare < 0) {
                left = restart_index_;
            } else if (current_key_compare > 0) {
                right = restart_index_;
            } else {
                return;
            }
        }

        while (left < right) {
            const uint32_t mid = (left + right + 1) / 2;
            const uint32_t region_offset = GetRestartPoint(mid);
            uint32_t shared = 0;
            uint32_t non_shared = 0;
            uint32_t value_length = 0;

            const char* key_ptr = DecodeEntry(data_ + region_offset, data_ + restarts_, &shared, &non_shared, &value_length);
            if (key_ptr == nullptr || shared != 0) {
                CorruptionError();
                return;
            }

            const Slice mid_key(key_ptr, non_shared);
            if (comparator_->Compare(mid_key, target) < 0) {
                left = mid;
            } else {
                right = mid - 1;
            }
        }

        const bool skip_seek = Valid() && left == restart_index_ && current_key_compare < 0;
        if (!skip_seek) {
            SeekToRestartPoint(left);
        }
        while (true) {
            if (!ParseNextKey()) {
                return;
            }
            if (comparator_->Compare(key_, target) >= 0) {
                return;
            }
        }
    }

    void Next() override {
        assert(Valid());
        ParseNextKey();
    }

    void Prev() override {
        assert(Valid());
        const uint32_t original = current_;
        while (GetRestartPoint(restart_index_) >= original) {
            if (restart_index_ == 0) {
                current_ = restarts_;
                restart_index_ = num_restarts_;

                return;
            }
            --restart_index_;
        }

        SeekToRestartPoint(restart_index_);
        do {
        } while (ParseNextKey() && NextEntryOffset() < original);
    }

    Slice key() const override {
        assert(Valid());
        return Slice(key_);
    }

    Slice value() const override {
        assert(Valid());
        return value_;
    }

    Status status() const override {
        return status_;
    }

private:
    uint32_t GetRestartPoint(uint32_t index) const {
        assert(index < num_restarts_);
        return DecodeFixed32(data_ + restarts_ + index*sizeof(uint32_t));
    }

    uint32_t NextEntryOffset() const {
        return static_cast<uint32_t>((value_.data() + value_.size()) - data_);
    }

    void SeekToRestartPoint(uint32_t index) {
        key_.clear();
        restart_index_ = index;
        const uint32_t offset = GetRestartPoint(index);
        value_ = Slice(data_ + offset, 0);
    }

    bool ParseNextKey() {
        current_ = NextEntryOffset();
        const char* p = data_ + current_;
        const char* limit = data_ + restarts_;
        if (p >= limit) {
            current_ = restarts_;
            restart_index_ = num_restarts_;

            return false;
        }

        uint32_t shared = 0;
        uint32_t non_shared = 0;
        uint32_t value_length = 0;
        p = DecodeEntry(p, limit, &shared, &non_shared, &value_length);
        if (p == nullptr || key_.size() < shared) {
            CorruptionError();
            return false;
        }

        key_.resize(shared);
        key_.append(p, non_shared);
        value_ = Slice(p + non_shared, value_length);
        while (restart_index_ + 1 < num_restarts_ && GetRestartPoint(restart_index_ + 1) < current_) {
            ++restart_index_;
        }
        return true;
    }

    void CorruptionError() {
        current_ = restarts_;
        restart_index_ = num_restarts_;
        key_.clear();
        value_.clear();
        status_ = Status::Corruption("bad entry in block");
    }

    const Comparator* comparator_;
    const char* data_;
    uint32_t num_restarts_;
    uint32_t restarts_;
    uint32_t current_;
    uint32_t restart_index_;
    std::string key_;
    Slice value_;
    Status status_;
};

}

uint32_t Block::NumRestarts() const {
    assert(size_ >= sizeof(uint32_t));
    return DecodeFixed32(data_ + size_ - sizeof(uint32_t));
}

Block::Block(const BlockContents& contents) 
    : data_(contents.data.data()), size_(contents.data.size()), restart_offset_(0), owned_(contents.heap_allocated) {
        if (size_ < sizeof(uint32_t)) {
            size_ = 0;
        } else {
            const size_t max_restarts_allowed = (size_ - sizeof(uint32_t)) / sizeof(uint32_t);
            if (NumRestarts() > max_restarts_allowed) {
                size_ = 0;
            } else {
                restart_offset_ = static_cast<uint32_t>(size_ - (1 + NumRestarts()) * sizeof(uint32_t));
            }
        }
    }

Block::~Block() {
    if (owned_) {
        delete[] data_;
    }
}

Iterator* Block::NewIterator(const Comparator* comparator) {
    if (size_ < sizeof(uint32_t)) {
        return NewErrorIterator(Status::Corruption("bad block contents"));
    }
    const uint32_t num_restarts = NumRestarts();
    if (num_restarts == 0) {
        return NewEmptyIterator();
    }
    return new BlockIter(comparator, data_, restart_offset_, num_restarts);
}
}