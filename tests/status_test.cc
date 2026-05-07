#include <string>
#include <utility>

#include <lindb/status.h>

#include "tests/test_util.h"

int main() {
    {
        lindb::Status ok;
        LINDB_EXPECT_TRUE(ok.ok());
        LINDB_EXPECT_EQ(ok.ToString(), std::string("OK"));
    }

    {
        lindb::Status not_found = lindb::Status::NotFound("missing", "key");
        LINDB_EXPECT_FALSE(not_found.ok());
        LINDB_EXPECT_TRUE(not_found.IsNotFound());
        LINDB_EXPECT_EQ(not_found.ToString(), std::string("NotFound: missing: key"));
    }

    {
        lindb::Status source = lindb::Status::Corruption("bad", "block");
        lindb::Status copy(source);
        LINDB_EXPECT_TRUE(copy.IsCorruption());
        LINDB_EXPECT_EQ(copy.ToString(), std::string("Corruption: bad: block"));

        lindb::Status assigned;
        assigned = source;
        LINDB_EXPECT_TRUE(assigned.IsCorruption());
        LINDB_EXPECT_EQ(assigned.ToString(), source.ToString());
    }

    {
        lindb::Status source = lindb::Status::IOError("disk", "failed");
        lindb::Status moved(std::move(source));
        LINDB_EXPECT_TRUE(moved.IsIOError());
        LINDB_EXPECT_EQ(moved.ToString(), std::string("IO error: disk: failed"));

        lindb::Status target;
        target = lindb::Status::InvalidArgument("bad", "input");
        LINDB_EXPECT_TRUE(target.IsInvalidArgument());
        LINDB_EXPECT_EQ(target.ToString(), std::string("Invalid argument: bad: input"));
    }

    return 0;
}
