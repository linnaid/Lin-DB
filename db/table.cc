#include <lindb/table.h>

#include <lindb/block.h>
#include <lindb/env.h>
#include <lindb/options.h>

#include "Lin-DB/db/format.h"
#include "Lin-DB/db/two_level_iterator.h"

namespace lindb {

struct Table::Rep {
    ~Rep() {
        delete index_block;
    }

    Options options;
    RandomAccessFile* file = nullptr;
    BlockHandle metaindex_handle;
    Block* index_block = nullptr;
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

        *table = new Table(rep);
        return Status::OK();
    }

Iterator* Table::BlockReader(void* arg, const ReadOptions& options, 
    const Slice& index_value) {
        const Table* table = reinterpret_cast<const Table*>(arg);
        BlockHandle handle;
        Slice input(index_value);
        Status status = handle.DecodeFrom(&input);

        Block* block = nullptr;
        if (status.ok()) {
            BlockContents contents;
            status = ReadBlock(table->rep_->file, options, handle, &contents);
            if (status.ok()) {
                block = new Block(contents);
            }
        }

        if (block == nullptr) {
            return NewErrorIterator(status);
        }

        Iterator* iterator = block->NewIterator(table->rep_->options.comparator);
        iterator->RegisterCleanup(&DeleteBlock, block, nullptr);
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
            Iterator* data_iter = BlockReader(const_cast<Table*>(this), options, index_iter->value());
            data_iter->Seek(key);
            if (data_iter->Valid()) {
                // CallBack 回调函数
                (*handle_result)(arg, data_iter->key(), data_iter->value());
            }
            status = data_iter->status();
            delete data_iter;
        }

        if (status.ok()) {
            status = index_iter->status();
        }

        delete index_iter;
        return status;
    }

}