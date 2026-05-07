#include <vector>

#include "db/skiplist.h"
#include "tests/test_util.h"
#include "util/arena.h"

namespace {

struct IntComparator {
    int operator()(int a, int b) const {
        if (a < b) {
            return -1;
        }
        if (a > b) {
            return 1;
        }
        return 0;
    }
};

}  // namespace

int main() {
    lindb::Arena arena;
    lindb::SkipList<int, IntComparator> list(IntComparator{}, &arena);

    list.Insert(10);
    list.Insert(20);
    list.Insert(30);

    LINDB_EXPECT_TRUE(list.Contains(10));
    LINDB_EXPECT_TRUE(list.Contains(20));
    LINDB_EXPECT_FALSE(list.Contains(25));

    lindb::SkipList<int, IntComparator>::Iterator iter(&list);
    iter.SeekToFirst();
    LINDB_EXPECT_TRUE(iter.Valid());
    LINDB_EXPECT_EQ(iter.key(), 10);
    iter.Next();
    LINDB_EXPECT_EQ(iter.key(), 20);
    iter.Seek(25);
    LINDB_EXPECT_TRUE(iter.Valid());
    LINDB_EXPECT_EQ(iter.key(), 30);
    iter.SeekToLast();
    LINDB_EXPECT_EQ(iter.key(), 30);
    iter.Prev();
    LINDB_EXPECT_EQ(iter.key(), 20);
    iter.Prev();
    LINDB_EXPECT_EQ(iter.key(), 10);
    iter.Prev();
    LINDB_EXPECT_FALSE(iter.Valid());

    return 0;
}
