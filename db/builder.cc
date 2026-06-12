#include "Lin-DB/db/builder.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>

#include <lindb/env.h>
#include <lindb/filename.h>
#include <lindb/iterator.h>
#include <lindb/table_builder.h>

#include "Lin-DB/db/version_edit.h"

namespace lindb {

Status BuildTable(const std::string& dbname, Env* env, const Options& options, Iterator* iter, FileMetaData* meta) {
    assert(env != nullptr);
    assert(iter != nullptr);
    assert(meta != nullptr);

    meta->file_size = 0;
    meta->smallest.Clear();
    meta->largest.Clear();
    iter->SeekToFirst();
    if (!iter->Valid()) {
        return iter->status();
    }

    const std::string table_name = TableFileName(dbname, meta->number);
    WritableFile* raw_file = nullptr;
    Status status = env->NewWritableFile(table_name, &raw_file);
    if (!status.ok()) {
        return status;
    }

    std::unique_ptr<WritableFile> file(raw_file);
    TableBuilder builder(options, file.get());

    meta->smallest.DecodeFrom(iter->key());
    for (; iter->Valid(); iter->Next()) {
        const Slice key = iter->key();
        meta->largest.DecodeFrom(key);
        builder.Add(key, iter->value());
    }

    if (iter->status().ok()) {
        status = builder.Finish();
    } else {
        builder.Abandon();
        status = iter->status();
    }
    
    if (status.ok()) {
        status = file->Sync();
    }

    if (status.ok()) {
        status = file->Close();
    } else {
        (void)file->Close();
    }

    if (status.ok()) {
        meta->file_size = builder.FileSize();
        uint64_t allowed_seeks = meta->file_size / 16384;
        if (allowed_seeks < 100) {
            allowed_seeks = 100;
        }
        if (allowed_seeks > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            allowed_seeks = static_cast<uint64_t>(std::numeric_limits<int>::max());
        }
        meta->allowed_seeks = static_cast<int>(allowed_seeks);
    } else {
        meta->file_size = 0;
        env->RemoveFile(table_name);
    }

    return status;
}

}