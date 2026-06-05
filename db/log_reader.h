// 声明 WAL Reader，负责从 SequentialFile 顺序读取 physical record，并拼回 logical record
#pragma once

#include <string>

#include <lindb/slice.h>
#include <lindb/status.h>

#include "Lin-DB/db/log_format.h"

namespace lindb {

class SequentialFile;

namespace log {

class Reader {
public:
    Reader(SequentialFile* file, bool checksum);
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool ReadRecord(std::string* record, Status* status);

private:
    unsigned int ReadPhysicalRecord(Slice* result, Status* status);

    SequentialFile* file_;
    bool checksum_;
    char backing_store_[kBlockSize];
    Slice buffer_;
    bool eof_;
};

}
}