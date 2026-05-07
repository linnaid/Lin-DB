#include <cstdint>

#include "util/random.h"
#include "tests/test_util.h"

int main() {
    lindb::Random a(301);
    lindb::Random b(301);
    for (int i = 0; i < 20; ++i) {
        LINDB_EXPECT_EQ(a.Next(), b.Next());
    }

    lindb::Random zero_seed(0);
    lindb::Random one_seed(1);
    LINDB_EXPECT_EQ(zero_seed.Next(), one_seed.Next());

    lindb::Random max_seed(2147483647L);
    lindb::Random normalized_seed(1);
    LINDB_EXPECT_EQ(max_seed.Next(), normalized_seed.Next());

    lindb::Random ranges(99);
    for (int i = 0; i < 100; ++i) {
        LINDB_EXPECT_TRUE(ranges.Uniform(7) < 7);
    }

    lindb::Random always_true(7);
    for (int i = 0; i < 10; ++i) {
        LINDB_EXPECT_TRUE(always_true.OneIn(1));
    }

    lindb::Random skewed(11);
    LINDB_EXPECT_TRUE(skewed.Skewed(4) < (1u << 4));

    return 0;
}
