#include <lindb/table.h>

#include <cstring>
#include <memory> 

#include <lindb/block.h>
#include <lindb/env.h>
#include <lindb/options.h>
#include <lindb/filter_policy.h>
#include <lindb/cache.h>
// #include <lindb/comparator.h>

#include "Lin-DB/db/format.h"
#include "Lin-DB/db/two_level_iterator.h"
#include "Lin-DB/db/filter_block.h"
#include "Lin-DB/util/coding.h"

namespace lindb {

struct Table::Rep {
    ~Rep() {
        delete filter;
        delete[] filter_data;
        delete index_block;
    }

    Options options;
    RandomAccessFile* file = nullptr;
    uint64_t cache_id = 0;
    BlockHandle metaindex_handle;
    Block* index_block = nullptr;
    FilterBlockReader* filter = nullptr;
    const char* filter_data = nullptr;
};

namespace {

void DeleteBlock(void* arg, void* ignored) {
    (void)ignored;
    delete reinterpret_cast<Block*>(arg);
}

}

Table::Table(Rep* rep) 
    : rep_(rep) {}

Table::~Table() {
    delete rep_;
}

Status Table::Open(const Options& options, RandomAccessFile* file, 
    uint64_t file_size, Table** table) {
        *table = nullptr;
        if (file_size < Footer::kEncodedLength) {
            return Status::Corruption("file is too short to be an sstable");
        }

        char footer_space[Footer::kEncodedLength];
        Slice footer_input;
        Status status = file->Read(file_size - Footer::kEncodedLength, Footer::kEncodedLength, &footer_input, footer_space);
        if (!status.ok()) {
            return status;
        }

        Footer footer;
        status = footer.DecodeFrom(&footer_input);
        if (!status.ok()) {
            return status;
        }

        ReadOptions read_options;
        read_options.verify_checksums = options.paranoid_checks;

        BlockContents index_contents;
        status = ReadBlock(file, read_options, footer.index_handle(), &index_contents);
        if (!status.ok()) {
            return status;
        }

        Rep* rep = new Rep;
        rep->options = options;
        rep->file = file;
        rep->metaindex_handle = footer.metaindex_handle();
        rep->index_block = new Block(index_contents);

        if (options.block_cache != nullptr) {
            rep->cache_id = options.block_cache->NewId();
        }

        *table = new Table(rep);
        (*table)->ReadMeta(footer);
        return Status::OK();
    }

Iterator* Table::BlockReader(void* arg, const ReadOptions& options, 
    const Slice& index_value) {
        const Table* table = reinterpret_cast<const Table*>(arg);
        Cache* block_cache = table->rep_->options.block_cache;
        Block* block = nullptr;
        Cache::Handle* cache_handle = nullptr;

        BlockHandle handle;
        Slice input(index_value);
        Status status = handle.DecodeFrom(&input);

        if (status.ok()) {
            if (block_cache != nullptr) {
                char cache_key[16];
                EncodeFixed64(cache_key, table->rep_->cache_id);
                EncodeFixed64(cache_key + 8, handle.offset());
                Slice key(cache_key, sizeof(cache_key));

                cache_handle = block_cache->Lookup(key);
                if (cache_handle != nullptr) {
                    block = reinterpret_cast<Block*>(block_cache->Value(cache_handle));
                } else {
                    BlockContents contents;
                    status = ReadBlock(table->rep_->file, options, handle, &contents);
                    if (status.ok()) {
                        block = new Block(contents);
                        if (contents.cachable && options.fill_cache) {
                            cache_handle = block_cache->Insert(key, block, block->size(), &DeleteCachedBlock);
                        }
                    }
                }
            } else {
                BlockContents contents;
                status = ReadBlock(table->rep_->file, options, handle, &contents);
                if (status.ok()) {
                    block = new Block(contents);
                }
            }
        }

        if (block == nullptr) {
            return NewErrorIterator(status);
        }

        Iterator* iterator = block->NewIterator(table->rep_->options.comparator);
        if (cache_handle == nullptr) {
            iterator->RegisterCleanup(&DeleteBlock, block, nullptr);
        } else {
            iterator->RegisterCleanup(&ReleaseCachedBlock, block_cache, cache_handle);
        }
        return iterator;
    }

Iterator* Table::NewIterator(const ReadOptions& options) const {
    return NewTwoLevelIterator(
        rep_->index_block->NewIterator(rep_->options.comparator),
        &Table::BlockReader,
        const_cast<Table*>(this),
        options
    );
}

Status Table::InternalGet(const ReadOptions& options, const Slice& key, 
    void* arg, void (*handle_result)(void*, const Slice&, const Slice&)) const {
        Status status;
        Iterator* index_iter = rep_->index_block->NewIterator(rep_->options.comparator);
        index_iter->Seek(key);

        if (index_iter->Valid()) {
            Slice handle_value(index_iter->value());
            BlockHandle handle;
            status = handle.DecodeFrom(&handle_value);
            if (status.ok()) {
                bool may_match = true;
                if (rep_->filter != nullptr) {
                    may_match = rep_->filter->KeyMayMatch(handle.offset(), key);
                }
                if (may_match) {
                    Iterator* data_iter = BlockReader(const_cast<Table*>(this), options, index_iter->value());
                    data_iter->Seek(key);
                    if (data_iter->Valid()) {
                        // CallBack 回调函数
                        (*handle_result)(arg, data_iter->key(), data_iter->value());
                    }
                    status = data_iter->status();
                    delete data_iter;
                }
            }
        }

        if (status.ok()) {
            status = index_iter->status();
        }

        delete index_iter;
        return status;
    }

// 读 metaindex block，找到 filter block 的位置，并通过 ReadFilter 读入
void Table::ReadMeta(const Footer& footer) {
    if (rep_->options.filter_policy == nullptr) {
        return;
    }

    ReadOptions options;
    options.verify_checksums = rep_->options.paranoid_checks;

    BlockContents contents;
    if (!ReadBlock(rep_->file, options, footer.metaindex_handle(), &contents).ok()) {
        return;
    }

    Block metaindex_block(contents);
    Iterator* iter = metaindex_block.NewIterator(BytewiseComparator());
    std::string key = "filter.";
    key.append(rep_->options.filter_policy->Name());
    iter->Seek(Slice(key));
    if (iter->Valid() && iter->key() == Slice(key)) {
        ReadFilter(iter->value());
    }
    delete iter;
}

// 读取 filter block 内容
void Table::ReadFilter(const Slice& filter_handle_value) {
    Slice input(filter_handle_value);
    BlockHandle filter_handle;
    if (!filter_handle.DecodeFrom(&input).ok()) {
        return;
    }

    ReadOptions options;
    options.verify_checksums = rep_->options.paranoid_checks;

    BlockContents contents;
    if(!ReadBlock(rep_->file, options, filter_handle, &contents).ok()) {
        return;
    }

    if (contents.heap_allocated) {
        rep_->filter_data = contents.data.data();
    } else {
        char* copy = new char[contents.data.size()];
        std::memcpy(copy, contents.data.data(), contents.data.size());
        rep_->filter_data = copy;
        contents.data = Slice(copy, contents.data.size());
    }

    rep_->filter = new FilterBlockReader(rep_->options.filter_policy, contents.data);
}

void DeleteCachedBlock(const Slice& key, void* value) {
    (void)key;
    delete reinterpret_cast<Block*>(value);
}

void ReleaseCachedBlock(void* cache, void* handle) {
    reinterpret_cast<Cache*>(cache)->Release(reinterpret_cast<Cache::Handle*>(handle));
}

}