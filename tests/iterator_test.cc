#include <string>

#include <lindb/iterator.h>

#include "tests/test_util.h"

namespace {

void CleanupCounter(void* arg1, void* arg2) {
    int* total = static_cast<int*>(arg1);
    int increment = *static_cast<int*>(arg2);
    *total += increment;
}

}  // namespace

int main() {
    {
        lindb::Iterator* iter = lindb::NewEmptyIterator();
        LINDB_EXPECT_FALSE(iter->Valid());
        iter->SeekToFirst();
        iter->SeekToLast();
        iter->Seek(lindb::Slice("target"));
        LINDB_EXPECT_TRUE(iter->status().ok());
        delete iter;
    }

    {
        lindb::Status error = lindb::Status::Corruption("bad", "iter");
        lindb::Iterator* iter = lindb::NewErrorIterator(error);
        LINDB_EXPECT_FALSE(iter->Valid());
        LINDB_EXPECT_TRUE(iter->status().IsCorruption());
        LINDB_EXPECT_EQ(iter->status().ToString(), std::string("Corruption: bad: iter"));
        delete iter;
    }

    {
        lindb::Iterator* iter = lindb::NewEmptyIterator();
        int total = 0;
        int one = 1;
        int two = 2;
        iter->RegisterCleanup(&CleanupCounter, &total, &one);
        iter->RegisterCleanup(&CleanupCounter, &total, &two);
        delete iter;
        LINDB_EXPECT_EQ(total, 3);
    }

    return 0;
}
