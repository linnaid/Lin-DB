#include <lindb/comparator.h>
#include <lindb/slice.h>

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
        };
    }

    const Comparator* BytewiseComparator() {
        static BytewiseComparatorImpl comparator;
        return &comparator;
    }
}