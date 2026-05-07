#include <cstdint>
#include <string>

#include "db/dbformat.h"
#include "util/coding.h"
#include "tests/test_util.h"

int main() {
    {
        const uint64_t tag = lindb::PackSequenceAndType(100, lindb::kTypeValue);
        LINDB_EXPECT_EQ(tag, (static_cast<uint64_t>(100) << 8) | lindb::kTypeValue);
    }

    {
        lindb::ParsedInternalKey parsed(lindb::Slice("user"), 123, lindb::kTypeDeletion);
        std::string encoded;
        lindb::AppendInternalKey(&encoded, parsed);

        lindb::ParsedInternalKey decoded;
        LINDB_EXPECT_TRUE(lindb::ParseInternalKey(lindb::Slice(encoded), &decoded));
        LINDB_EXPECT_EQ(decoded.user_key.ToString(), std::string("user"));
        LINDB_EXPECT_EQ(decoded.sequence, static_cast<lindb::SequenceNumber>(123));
        LINDB_EXPECT_EQ(decoded.type, lindb::kTypeDeletion);
        LINDB_EXPECT_EQ(lindb::ExtractUserKey(lindb::Slice(encoded)).ToString(), std::string("user"));
    }

    {
        lindb::InternalKeyComparator comparator(lindb::BytewiseComparator());
        lindb::InternalKey newer("key", 200, lindb::kTypeValue);
        lindb::InternalKey older("key", 100, lindb::kTypeValue);
        lindb::InternalKey other("zkey", 150, lindb::kTypeValue);

        LINDB_EXPECT_TRUE(comparator.Compare(newer, older) < 0);
        LINDB_EXPECT_TRUE(comparator.Compare(older, newer) > 0);
        LINDB_EXPECT_TRUE(comparator.Compare(newer, other) < 0);
    }

    {
        lindb::LookupKey lookup("abc", 99);
        LINDB_EXPECT_EQ(lookup.user_key().ToString(), std::string("abc"));
        LINDB_EXPECT_EQ(lookup.internal_key().size(), static_cast<size_t>(11));
        LINDB_EXPECT_TRUE(lookup.memtable_key().size() > lookup.internal_key().size());
    }

    {
        lindb::ParsedInternalKey out;
        LINDB_EXPECT_FALSE(lindb::ParseInternalKey(lindb::Slice("short"), &out));

        std::string encoded = "k";
        lindb::PutFixed64(&encoded, (static_cast<uint64_t>(7) << 8) | 9);
        LINDB_EXPECT_FALSE(lindb::ParseInternalKey(lindb::Slice(encoded), &out));
    }

    return 0;
}
