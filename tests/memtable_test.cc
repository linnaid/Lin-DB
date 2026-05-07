#include <string>

#include <lindb/iterator.h>

#include "db/dbformat.h"
#include "db/memtable.h"
#include "tests/test_util.h"

int main() {
    lindb::InternalKeyComparator comparator(lindb::BytewiseComparator());
    lindb::MemTable table(comparator);

    table.Add(100, lindb::kTypeValue, lindb::Slice("k1"), lindb::Slice("v1"));
    table.Add(101, lindb::kTypeValue, lindb::Slice("k1"), lindb::Slice("v2"));
    table.Add(102, lindb::kTypeDeletion, lindb::Slice("k1"), lindb::Slice(""));
    table.Add(103, lindb::kTypeValue, lindb::Slice("k2"), lindb::Slice("v3"));

    std::string value;

    {
        lindb::LookupKey key("k1", 101);
        LINDB_EXPECT_TRUE(table.Get(key, &value));
        LINDB_EXPECT_EQ(value, std::string("v2"));
    }

    {
        lindb::LookupKey key("k1", 100);
        LINDB_EXPECT_TRUE(table.Get(key, &value));
        LINDB_EXPECT_EQ(value, std::string("v1"));
    }

    {
        lindb::LookupKey key("k1", 102);
        LINDB_EXPECT_FALSE(table.Get(key, &value));
    }

    {
        lindb::LookupKey key("k2", 103);
        LINDB_EXPECT_TRUE(table.Get(key, &value));
        LINDB_EXPECT_EQ(value, std::string("v3"));
    }

    {
        lindb::LookupKey key("missing", 200);
        LINDB_EXPECT_FALSE(table.Get(key, &value));
    }

    {
        lindb::Iterator* iter = table.NewIterator();
        iter->SeekToFirst();
        LINDB_EXPECT_TRUE(iter->Valid());

        lindb::ParsedInternalKey parsed;
        LINDB_EXPECT_TRUE(lindb::ParseInternalKey(iter->key(), &parsed));
        LINDB_EXPECT_EQ(parsed.user_key.ToString(), std::string("k1"));
        LINDB_EXPECT_EQ(parsed.sequence, static_cast<lindb::SequenceNumber>(102));
        LINDB_EXPECT_EQ(parsed.type, lindb::kTypeDeletion);
        LINDB_EXPECT_EQ(iter->value().ToString(), std::string(""));

        iter->Next();
        LINDB_EXPECT_TRUE(iter->Valid());
        LINDB_EXPECT_TRUE(lindb::ParseInternalKey(iter->key(), &parsed));
        LINDB_EXPECT_EQ(parsed.user_key.ToString(), std::string("k1"));
        LINDB_EXPECT_EQ(parsed.sequence, static_cast<lindb::SequenceNumber>(101));
        LINDB_EXPECT_EQ(parsed.type, lindb::kTypeValue);
        LINDB_EXPECT_EQ(iter->value().ToString(), std::string("v2"));

        iter->Next();
        LINDB_EXPECT_TRUE(iter->Valid());
        LINDB_EXPECT_TRUE(lindb::ParseInternalKey(iter->key(), &parsed));
        LINDB_EXPECT_EQ(parsed.user_key.ToString(), std::string("k1"));
        LINDB_EXPECT_EQ(parsed.sequence, static_cast<lindb::SequenceNumber>(100));
        LINDB_EXPECT_EQ(parsed.type, lindb::kTypeValue);
        LINDB_EXPECT_EQ(iter->value().ToString(), std::string("v1"));

        iter->Next();
        LINDB_EXPECT_TRUE(iter->Valid());
        LINDB_EXPECT_TRUE(lindb::ParseInternalKey(iter->key(), &parsed));
        LINDB_EXPECT_EQ(parsed.user_key.ToString(), std::string("k2"));
        LINDB_EXPECT_EQ(parsed.sequence, static_cast<lindb::SequenceNumber>(103));
        LINDB_EXPECT_EQ(parsed.type, lindb::kTypeValue);
        LINDB_EXPECT_EQ(iter->value().ToString(), std::string("v3"));

        iter->Next();
        LINDB_EXPECT_FALSE(iter->Valid());
        delete iter;
    }

    {
        lindb::Iterator* iter = table.NewIterator();
        lindb::InternalKey target("k1", 101, lindb::kValueTypeForSeek);
        iter->Seek(target.Encode());
        LINDB_EXPECT_TRUE(iter->Valid());

        lindb::ParsedInternalKey parsed;
        LINDB_EXPECT_TRUE(lindb::ParseInternalKey(iter->key(), &parsed));
        LINDB_EXPECT_EQ(parsed.user_key.ToString(), std::string("k1"));
        LINDB_EXPECT_EQ(parsed.sequence, static_cast<lindb::SequenceNumber>(101));
        LINDB_EXPECT_EQ(parsed.type, lindb::kTypeValue);
        LINDB_EXPECT_EQ(iter->value().ToString(), std::string("v2"));

        lindb::InternalKey tail("zz", 1, lindb::kTypeValue);
        iter->Seek(tail.Encode());
        LINDB_EXPECT_FALSE(iter->Valid());
        delete iter;
    }

    LINDB_EXPECT_TRUE(table.ApproximateMemoryUsage() > 0);

    return 0;
}
