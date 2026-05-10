#include <lindb/comparator.h>
#include <lindb/slice.h>

#include <algorithm>
#include <cstdint>

namespace lindb {
namespace {
class BytewiseComparatorImpl : public Comparator {
public:
    int Compare(const Slice& a, const Slice& b) const override {
        return a.compare(b);
    }       

    const char* Name() const override {
        return "lindb.BytewiseComparator";
    }

    void FindShortestSeparator(std::string* start, const Slice& limit) const override {
        const size_t min_length = std::min(start->size(), limit.size());
        size_t diff_index = 0;

        while (diff_index < min_length && (*start)[diff_index] == limit.data()[diff_index]) {
            ++diff_index;
        }

        if (diff_index >= min_length) {
            return;
        }

        const uint8_t start_byte = static_cast<uint8_t>((*start)[diff_index]);
        const uint8_t limit_byte = static_cast<uint8_t>(limit.data()[diff_index]);

        if (start_byte < static_cast<uint8_t>(0xff) && start_byte + 1 < limit_byte) {
            (*start)[diff_index] = static_cast<char>(start_byte + 1);
            start->resize(diff_index + 1);
        }
    }

    void FindShortSuccessor(std::string* key) const override {
        for (size_t i = 0; i < key->size(); ++i) {
            const uint8_t byte = static_cast<uint8_t>((*key)[i]);

            if (byte != static_cast<uint8_t>(0xff)) {
                (*key)[i] = static_cast<char>(byte + 1);
                key->resize(i + 1);
                return;
            }
        }
    }
};

}

const Comparator* BytewiseComparator() {
    static BytewiseComparatorImpl comparator;
    return &comparator;
}
}