#include <lindb/table_builder.h>

#include <cassert>

#include <lindb/options.h>
#include <lindb/block_builder.h>
#include "Lin-DB/db/format.h"
#include "Lin-DB/util/crc32c.h"
#include "Lin-DB/util/coding.h"

namespace lindb {

struct TableBuilder::Rep {
    Rep(const Options& opt, WritableFile* f)
        : options(opt), index_block_options(opt), file(f), offset(0),
          data_block(&options), index_block(&index_block_options), 
          num_entries(0), closed(false), pending_index_entry(false) {
            index_block_options.block_restart_interval = 1;
          }

    Options options;
    Options index_block_options;
    WritableFile* file;
    uint64_t offset;
    Status status;
    BlockBuilder data_block;
    BlockBuilder index_block;
    std::string last_key;
    uint64_t num_entries;
    bool closed;
    // 上一个 data block 的 index entry 是否还没有写入 index block
    bool pending_index_entry;
    // 上一个 data block 的handle
    BlockHandle pending_handle;
};

TableBuilder::TableBuilder(const Options& options, WritableFile* file) 
    : rep_(new Rep(options, file)) {}

TableBuilder::~TableBuilder() {
    assert(rep_->closed);
    delete rep_;
}

Status TableBuilder::ChangeOptions(const Options& options) {
    if (options.comparator != rep_->options.comparator) {
        return Status::InvalidArgument("changing comparator while building table");
    }
    rep_->options = options;
    rep_->index_block_options = options;
    rep_->index_block_options.block_restart_interval = 1;
    return Status::OK();
}

void TableBuilder::Add(const Slice& key, const Slice& value) {
    Rep* r = rep_;
    assert(!r->closed);
    if (!ok()) return;
    if (r->num_entries > 0) {
        assert(r->options.comparator->Compare(key, Slice(r->last_key)) > 0);
    }

    if (r->pending_index_entry) {
        assert(r->data_block.empty());
        r->options.comparator->FindShortestSeparator(&r->last_key, key);
        std::string handle_encoding;
        r->pending_handle.EncodeTo(&handle_encoding);
        r->index_block.Add(r->last_key, Slice(handle_encoding));
        r->pending_index_entry = false;
    }

    r->last_key.assign(key.data(), key.size());
    ++r->num_entries;
    r->data_block.Add(key, value);

    if (r->data_block.CurrentSizeEstimate() >= r->options.block_size) {
        Flush();
    }
}

void TableBuilder::Flush() {
    Rep* r = rep_;
    assert(!r->closed);
    if (!ok()) return;
    if (r->data_block.empty()) return;
    assert(!r->pending_index_entry);

    WriteBlock(&r->data_block, &r->pending_handle);
    if (ok()) {
        r->pending_index_entry = true;
        r->status = r->file->Flush();
    }
}

Status TableBuilder::status() const {
    return rep_->status;
}

void TableBuilder::WriteBlock(BlockBuilder* block, BlockHandle* handle) {
    Rep* r = rep_;
    assert(ok());
    Slice raw = block->Finish();
    WriteRawBlock(raw, kNoCompression, handle);
    block->Reset();
}

void TableBuilder::WriteRawBlock(const Slice& data, CompressionType type, BlockHandle* handle) {
    Rep* r= rep_;
    handle->set_offset(r->offset);
    handle->set_size(data.size());
    r->status = r->file->Append(data);
    if (!r->status.ok()) return;

    char trailer[kBlockTrailerSize];
    trailer[0] = static_cast<char>(type);
    uint32_t crc = crc32c::Value(data.data(), data.size());
    crc = crc32c::Extend(crc, trailer, 1);
    EncodeFixed32(trailer + 1, crc32c::Mask(crc));
    r->status = r->file->Append(Slice(trailer, kBlockTrailerSize));
    if (r->status.ok()) {
        r->offset += data.size() + kBlockTrailerSize;
    }
}

Status TableBuilder::Finish() {
    Rep* r = rep_;
    Flush();
    assert(!r->closed);
    r->closed = true;

    BlockHandle metaindex_handle;
    BlockHandle index_handle;

    if (ok()) {
        BlockBuilder metaindex_block(&r->options);
        WriteBlock(&metaindex_block, &metaindex_handle);
    }

    if (ok()) {
        if (r->pending_index_entry) {
            r->options.comparator->FindShortSuccessor(&r->last_key);
            std::string handle_encoding;
            r->pending_handle.EncodeTo(&handle_encoding);
            r->index_block.Add(r->last_key, Slice(handle_encoding));
            r->pending_index_entry = false;
        }
        WriteBlock(&r->index_block, &index_handle);
    }

    if (ok()) {
        Footer footer;
        footer.set_metaindex_handle(metaindex_handle);
        footer.set_index_handle(index_handle);
        std::string footer_encoding;
        footer.EncodeTo(&footer_encoding);
        r->status = r->file->Append(Slice(footer_encoding));
        if (r->status.ok()) {
            r->offset += footer_encoding.size();
        }
    }

    return r->status;
}

void TableBuilder::Abandon() {
    Rep* r = rep_;
    assert(!r->closed);
    r->closed = true;
}

uint64_t TableBuilder::NumEntries() const {
    return rep_->num_entries;
}

uint64_t TableBuilder::FileSize() const {
    return rep_->offset;
}

}