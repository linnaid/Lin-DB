#include <cstddef>
#include <cstdint>
#include <cstring>

#include "util/arena.h"
#include "tests/test_util.h"

int main() {
    lindb::Arena arena;

    char* first = arena.Allocate(16);
    std::memcpy(first, "arena-test", 11);
    LINDB_EXPECT_EQ(std::memcmp(first, "arena-test", 10), 0);

    const size_t before = arena.MemoryUsage();
    char* aligned = arena.AllocateAligned(32);
    const uintptr_t address = reinterpret_cast<uintptr_t>(aligned);
    LINDB_EXPECT_EQ(address % 8, static_cast<uintptr_t>(0));
    LINDB_EXPECT_TRUE(arena.MemoryUsage() >= before);

    char* large = arena.Allocate(2048);
    large[0] = 'x';
    large[2047] = 'y';
    LINDB_EXPECT_EQ(large[0], 'x');
    LINDB_EXPECT_EQ(large[2047], 'y');
    LINDB_EXPECT_TRUE(arena.MemoryUsage() > 0);

    return 0;
}
