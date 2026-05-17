#include "lindb/filename.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include <lindb/env.h>
#include <lindb/slice.h>
#include "Lin-DB/util/logging.h"

namespace lindb {
namespace {

std::string MaskFileName(const std::string& dbname, uint64_t number, const char* suffix) {
    char buf[100];
    std::snprintf(buf, sizeof(buf), "/%06llu.%s", static_cast<unsigned long long>(number), suffix);
    return dbname + buf;
}

bool SliceEquals(const Slice& slice, const char* literal) {
    const size_t literal_size = std::strlen(literal);
    return slice.size() == literal_size && std::memcmp(slice.data(), literal, literal_size) == 0;
}

bool SliceStartsWith(const Slice& slice, const char* prefix) {
    const size_t prefix_size = std::strlen(prefix);
    return slice.size() >= prefix_size && std::memcmp(slice.data(), prefix, prefix_size) == 0;
}
}

std::string LogFileName(const std::string& dbname, uint64_t number) {
    assert(number > 0);
    return MaskFileName(dbname, number, "log");
}

std::string TableFileName(const std::string& dbname, uint64_t number) {
    assert(number > 0);
    return MaskFileName(dbname, number, "ldb");
}

std::string DescriptorFileName(const std::string& dbname, uint64_t number) {
    assert(number > 0);
    char buf[100];
    std::snprintf(buf, sizeof(buf), "/MANIFEST-%06llu", static_cast<unsigned long long>(number));
    return dbname + buf;
}

std::string CurrentFileName(const std::string& dbname) {
    return dbname + "/CURRENT";
}

std::string TempFileName(const std::string& dbname, uint64_t number) {
    assert(number > 0);
    return MaskFileName(dbname, number, "dbtmp");
}

std::string InfoLogFileName(const std::string& dbname) {
    return dbname + "/LOG";
}

std::string OldInfoLogFileName(const std::string& dbname) {
    return dbname + "/LOG.old";
}

bool ParseFileName(const std::string& filename, uint64_t* number, FileType* type) {
    Slice rest(filename);

    if (SliceEquals(rest, "CURRENT")) {
        *number = 0;
        *type = kCurrentFile;
        return true;
    }

    if (SliceEquals(rest, "LOCK")) {
        *number = 0;
        *type = kDBLockFile;
        return true;
    }

    if (SliceEquals(rest, "LOG") || SliceEquals(rest, "LOG.old")) {
        *number = 0;
        *type = kInfoLogFile;
        return true;
    }

    if (SliceStartsWith(rest, "MANIFEST-")) {
        rest.remove_prefix(std::strlen("MANIFEST-"));
        uint64_t num = 0;
        if (!ConsumeDecimalNumber(&rest, &num) || !rest.empty()) {
            return false;
        }
        *number = num;
        *type = kDescriptorFile;
        return true;
    }

    uint64_t num = 0;
    if (!ConsumeDecimalNumber(&rest, &num)) {
        return false;
    }

    if (SliceEquals(rest, ".log")) {
        *number = num;
        *type = kLogFile;
        return true;
    }

    if (SliceEquals(rest, ".sst") || SliceEquals(rest, ".ldb")) {
        *number = num;
        *type = kTableFile;
        return true;
    }

    if (SliceEquals(rest, ".dbtmp")) {
        *number = num;
        *type = kTempFile;
        return true;
    }

    return false;
}

Status SetCurrentFile(Env* env, const std::string& dbname, uint64_t descriptor_number) {
    const std::string manifest = DescriptorFileName(dbname, descriptor_number);
    assert(manifest.size()  > dbname.size() + 1);
    Slice contents(manifest);
    contents.remove_prefix(dbname.size() + 1);
    std::string current = contents.ToString();
    current.push_back('\n');
    const std::string tmp = TempFileName(dbname, descriptor_number);

    Status status = WriteStringToFileSync(env, Slice(current), tmp);
    if (status.ok()) {
        status = env->RenameFile(tmp, CurrentFileName(dbname));
    }
    
    if (!status.ok()) {
        env->RemoveFile(tmp);
    }

    return status;
}

}