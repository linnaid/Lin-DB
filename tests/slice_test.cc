#include <string>

#include <lindb/slice.h>

#include "tests/test_util.h"

int main() {
    lindb::Slice empty;
    LINDB_EXPECT_TRUE(empty.empty());
    LINDB_EXPECT_EQ(empty.size(), static_cast<size_t>(0));

    lindb::Slice from_cstr("hello");
    LINDB_EXPECT_EQ(from_cstr.size(), static_cast<size_t>(5));
    LINDB_EXPECT_EQ(from_cstr.ToString(), std::string("hello"));

    std::string backing = "prefix-value";
    lindb::Slice from_string(backing);
    from_string.remove_prefix(7);
    LINDB_EXPECT_EQ(from_string.ToString(), std::string("value"));

    lindb::Slice a("abc");
    lindb::Slice b("abd");
    lindb::Slice c("abc");
    lindb::Slice d("ab");
    LINDB_EXPECT_TRUE(a.compare(b) < 0);
    LINDB_EXPECT_TRUE(b.compare(a) > 0);
    LINDB_EXPECT_EQ(a.compare(c), 0);
    LINDB_EXPECT_TRUE(d.compare(a) < 0);

    empty.clear();
    LINDB_EXPECT_TRUE(empty.empty());

    return 0;
}
