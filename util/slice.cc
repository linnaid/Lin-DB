#include <lindb/slice.h>

#include <cassert>
#include <cstring>

namespace lindb {
    Slice::Slice() : data_(""), size_(0) {}

    Slice::Slice(const char* data, size_t size) : data_(data), size_(size) {}

    Slice::Slice(const std::string& str) : data_(str.data()), size_(str.size()) {}

    Slice::Slice(const char* str) : data_(str), size_(std::strlen(str)) {}

    const char* Slice::data() const { return data_; }

    size_t Slice::size() const { return size_; }

    bool Slice::empty() const { return size_ == 0; }

    void Slice::clear() { data_ = ""; size_ = 0; }

    void Slice::remove_prefix(size_t n) {
        assert(n <= size_);
        data_ += n;
        size_ -= n;
    }

    std::string Slice::ToString() const {
        return std::string(data_, size_);
    }

    int Slice::compare(const Slice& other) const {
        const size_t min_len = (size_ < other.size_) ? size_ : other.size_;
        int result = std::memcmp(data_, other.data_, min_len);
        if (result == 0) {
            if (size_ < other.size_) {
                return -1;
            } else if (size_ > other.size_) {
                return 1;
            }
        }
        return result;
    }

}