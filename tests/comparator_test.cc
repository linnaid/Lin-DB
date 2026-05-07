#include <string>

#include <lindb/comparator.h>
#include <lindb/slice.h>

#include "tests/test_util.h"

int main() {
    const lindb::Comparator* comparator = lindb::BytewiseComparator();

    LINDB_EXPECT_TRUE(comparator->Compare(lindb::Slice("abc"), lindb::Slice("abd")) < 0);
    LINDB_EXPECT_TRUE(comparator->Compare(lindb::Slice("abd"), lindb::Slice("abc")) > 0);
    LINDB_EXPECT_EQ(comparator->Compare(lindb::Slice("same"), lindb::Slice("same")), 0);
    LINDB_EXPECT_EQ(std::string(comparator->Name()), std::string("lindb.BytewiseComparator"));

    return 0;
}
