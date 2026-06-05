#include "Lin-DB/db/table_cache.h"

#include <cassert>
#include <memory>

#include <lindb/env.h>
#include <lindb/filename.h>
#include <lindb/iterator.h>
#include <lindb/slice.h>

#include "Lin-DB/util/coding.h"

namespace lindb {
namespace {

struct TableAndFile {
    RandomAccessFile* file = nullptr;
    Table* table = nullptr;
};

void DeleteEntry(const Slice& key, void* value) {
    (void)key;
    TableAndFile* entry = reinterpret_cast<TableAndFile*>(value);
    delete entry->table;
    delete entry->file;
    delete entry;
}

void UnrefEntry(void* cache, void* handle) {
    reinterpret_cast<Cache*>(cache)->Release(reinterpret_cast<Cache::Handle*>(handle));
}

}

TableCache::TableCache(const std::string& dbname, const Options& options, int entries)
    : dbname_(dbname), options_(options), cache_(NewLRUCache(entries)) {}

TableCache::~TableCache() {
    delete cache_;
}

Status TableCache::FindTable(uint64_t file_number, uint64_t file_size, Cache::Handle** handle) {
    char cache_key_buffer[8];
    EncodeFixed64(cache_key_buffer, file_number);
    Slice cache_key(cache_key_buffer, sizeof(cache_key_buffer));

    *handle = cache_->Lookup(cache_key);
    if (*handle != nullptr) {
        return Status::OK();
    }

    const std::string fname = TableFileName(dbname_, file_number);
    RandomAccessFile* raw_file = nullptr;
    Status status = options_.env->NewRandomAccessFile(fname, &raw_file);
    if (!status.ok()) {
        return status;
    }

    std::unique_ptr<RandomAccessFile> file(raw_file);
    Table* raw_table = nullptr;
    status = Table::Open(options_, file.get(), file_size, &raw_table);
    if (!status.ok()) {
        return status;
    }

    std::unique_ptr<Table> table(raw_table);
    TableAndFile* entry = new TableAndFile;
    entry->file = file.release();
    entry->table = table.release();

    *handle = cache_->Insert(cache_key, entry, 1, &DeleteEntry);
    return Status::OK();
}

Iterator* TableCache::NewIterator(const ReadOptions& options, uint64_t file_number, uint64_t file_size) {
    Cache::Handle* handle = nullptr;
    Status status = FindTable(file_number, file_size, &handle);
    if (!status.ok()) {
        return NewErrorIterator(status);
    }

    TableAndFile* entry = reinterpret_cast<TableAndFile*>(cache_->Value(handle));
    Iterator* iter = entry->table->NewIterator(options);
    iter->RegisterCleanup(&UnrefEntry, cache_, handle);
    return iter;
} 

Status TableCache::Get(const ReadOptions& options, uint64_t file_number, uint64_t file_size, const Slice& key, 
                       void* arg, void (*handle_result)(void*, const Slice&, const Slice&)) {
    Cache::Handle* handle = nullptr;
    Status status = FindTable(file_number, file_size, &handle);
    if (status.ok()) {
        TableAndFile* entry = reinterpret_cast<TableAndFile*>(cache_->Value(handle));
        status = entry->table->InternalGet(options, key, arg, handle_result);
        cache_->Release(handle);
    }
    return status;
}

void TableCache::Evict(uint64_t file_number) {
    char cache_key_buffer[8];
    EncodeFixed64(cache_key_buffer, file_number);
    cache_->Erase(Slice(cache_key_buffer, sizeof(cache_key_buffer)));
}

}