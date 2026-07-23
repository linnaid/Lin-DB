#include  "Lin-DB/db/db_impl.h"

#include <cassert>
#include <utility>
#include <vector>
#include <algorithm>
#include <set>

#include <lindb/write_batch.h>
#include <lindb/env.h>
#include <lindb/filename.h>
#include <lindb/iterator.h>
#include <lindb/table_builder.h>

#include "Lin-DB/db/write_batch_internal.h"
#include "Lin-DB/db/db_iter.h"
#include "Lin-DB/db/merger.h"
#include "Lin-DB/db/table_cache.h"
#include "Lin-DB/db/version_set.h"
#include "Lin-DB/db/builder.h"
#include "Lin-DB/db/version_edit.h"
#include "Lin-DB/db/log_reader.h"

namespace lindb {
SnapshotImpl::SnapshotImpl(SequenceNumber sequence)
    : sequence_(sequence) {}

SequenceNumber SnapshotImpl::sequence() const {
    return sequence_;
}

namespace {
Options MakeTableOptions(const Options& options, const InternalKeyComparator* internal_comparator) {
    Options table_options = options;
    table_options.comparator = internal_comparator;
    return table_options;
}

int TableCacheEntries(int max_open_files) {
    const int reserved_files = 10;
    if (max_open_files > reserved_files) {
        return max_open_files - reserved_files;
    }
    return 1;
}

constexpr size_t kWriteBatchHeaderSize = 12;

// 判断目录枚举结果是否是 POSIX 特殊目录项("."/"..")
bool IsDotOrDotDot(const std::string& filename) {
    return filename == "." || filename == "..";
}

// Iterator cleanup 回调，用来释放 Version 引用
void UnrefVersion(void* arg1, void* arg2) {
    (void)arg2;
    Version* version = reinterpret_cast<Version*>(arg1);
    version->Unref();
}
}

struct DBImpl::CompactionState {
    // 由 compaction 生成的输出 SSTable
    struct Output {
        uint64_t number = 0;
        uint64_t file_size = 0;
        InternalKey smallest;
        InternalKey largest;
    };
    // 返回outputs最后一个(当前正在写入的)SST元数据
    Output* current_output() {
        assert(!outputs.empty());
        return &outputs.back();
    }
    explicit CompactionState(Compaction* compaction_arg)
        : compaction(compaction_arg),
          smallest_snapshot(0),
          outfile(nullptr),
          builder(nullptr),
          total_bytes(0) {
        assert(compaction != nullptr);
    }
    Compaction* const compaction;
    SequenceNumber smallest_snapshot;
    std::vector<Output> outputs;
    WritableFile* outfile;
    TableBuilder* builder;
    uint64_t total_bytes;
};

DBImpl::DBImpl(const Options& options, std::string dbname)
    : options_(options),
      dbname_(std::move(dbname)),
      internal_comparator_(options_.comparator),
      table_options_(MakeTableOptions(options_, &internal_comparator_)),
      table_cache_(dbname_, table_options_, TableCacheEntries(options_.max_open_files)),
      versions_(dbname_, &table_options_, &table_cache_, &internal_comparator_),
      mem_(std::make_unique<MemTable>(internal_comparator_)),
      imm_(nullptr),
      last_sequence_(0),
      logfile_number_(1) {}

DBImpl::~DBImpl() {
    // 开启一个局部作用域，让锁在关闭WAL前释放
    {
        std::unique_lock<std::mutex> lock(background_mu_);
        shutting_down_ = true;
        background_cv_.wait(lock, [this]() {
            return !background_compaction_running_ && !background_compaction_scheduled_;
        });
    }
    // 释放 WAL writer，确保不再写日志记录
    log_.reset();
    if (log_file_ != nullptr) {
        (void)log_file_->Close();
        delete log_file_;
        log_file_ = nullptr;
    }
}

Status DBImpl::InitWAL() {
    if (!options_.env->FileExists(dbname_)) {
        Status s = options_.env->CreateDir(dbname_);
        if (!s.ok()) {
            return s;
        }
    }

    const std::string log_name = LogFileName(dbname_, logfile_number_);
    WritableFile* file = nullptr;
    Status s = options_.env->NewWritableFile(log_name, &file);
    if (!s.ok()) {
        return s;
    }

    log_file_ = file;
    log_ = std::make_unique<log::Writer>(log_file_);
    return Status::OK();
}

Status DBImpl::Put(const WriteOptions& options, const Slice& key, const Slice& value) {
    // (void)options;
    WriteBatch batch;
    batch.Put(key, value);
    return Write(options, &batch);

    // const SequenceNumber sequence = NewSequence();
    // mem_.Add(sequence, kTypeValue, key, value);
    // return Status::OK();
}

Status DBImpl::Delete(const WriteOptions& options, const Slice& key) {
    WriteBatch batch;
    batch.Delete(key);
    return Write(options, &batch);
}

Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
    if (updates == nullptr) {
        return Status::InvalidArgument("DBImpl::Write updates is null");
    }

    const int count = WriteBatchInternal::Count(updates);
    if (count == 0) {
        return Status::OK();
    }

    Status s = MakeRoomForWrite();
    if (!s.ok()) {
        return s;
    }

    assert(last_sequence_ <= kMaxSequenceNumber - static_cast<SequenceNumber>(count));
    const SequenceNumber first_sequence = last_sequence_ + 1;
    WriteBatchInternal::SetSequence(updates, first_sequence);

    s = log_->AddRecord(WriteBatchInternal::Contents(updates));
    if (!s.ok()) {
        return s;
    }

    if (options.sync) {
        s = log_file_->Sync();
        if (!s.ok()) {
            return s;
        }
    }

    s = WriteBatchInternal::InsertInto(updates, mem_.get());
    if(s.ok()) {
        last_sequence_ += static_cast<SequenceNumber>(count);
        MaybeScheduleCompaction();
    }

    return s;
}

Status DBImpl::Get(const ReadOptions& options, const Slice& key, std::string* value) {
    if (value == nullptr) {
        return Status::InvalidArgument("DBImpl::Get value is null");
    }

    LookupKey lookup_key(key, GetSnapshotSequence(options));
    Status status;

    if(mem_->Get(lookup_key, value, &status)) {
        return status;
    }

    if (imm_ != nullptr && imm_->Get(lookup_key, value, &status)) {
        return status;
    }

    status = versions_.current()->Get(options, lookup_key, value);
    if (!status.ok()) {
        value->clear();
    }
    return status;
}

Iterator* DBImpl::NewIterator(const ReadOptions& options) {
    SequenceNumber sequence = GetSnapshotSequence(options);
    std::vector<Iterator*> children;
    children.push_back(mem_->NewIterator());
    if (imm_ != nullptr) {
        children.push_back(imm_->NewIterator());
    }
    Version* current = versions_.current();
    current->Ref();
    current->AddIterators(options, &children);
    Iterator* internal_iter = NewMergingIterator(&internal_comparator_, std::move(children)); // // //
    internal_iter->RegisterCleanup(&UnrefVersion, current, nullptr);
    return NewDBIterator(options_.comparator, internal_iter, sequence);
}

const Snapshot* DBImpl::GetSnapshot() {
    const SequenceNumber sequence = last_sequence_;
    snapshots_.insert(sequence);
    return new SnapshotImpl(sequence);
}

void DBImpl::ReleaseSnapshot(const Snapshot* snapshot) {
    if (snapshot == nullptr) {
        return;
    }
    const SnapshotImpl* snapshot_impl = static_cast<const SnapshotImpl*>(snapshot);
    auto iter = snapshots_.find(snapshot_impl->sequence());
    if (iter != snapshots_.end()) {
        snapshots_.erase(iter);
    }
    delete snapshot_impl;
}

SequenceNumber DBImpl::SmallestSnapshot() const {
    if (snapshots_.empty()) {
        return last_sequence_;
    }
    return *snapshots_.begin();
}

SequenceNumber DBImpl::GetSnapshotSequence(const ReadOptions& options) const {
    if (options.snapshot == nullptr) {
        return last_sequence_;
    }
    const SnapshotImpl* snapshot = static_cast<const SnapshotImpl*>(options.snapshot);
    return snapshot->sequence();
}

SequenceNumber DBImpl::NewSequence() {
    assert(last_sequence_ < kMaxSequenceNumber);
    ++last_sequence_;
    return last_sequence_;
}

// 打开 DB 时恢复 MANIFEST、重放 WAL，并创建新的当前 WAL，并收口 recovery 后的 log_number 元数据语义
Status DBImpl::Open() {
    if (!options_.env->FileExists(dbname_)) {
        Status status = options_.env->CreateDir(dbname_);
        if (!status.ok()) {
            return status;
        }
    }

    std::vector<std::string> filenames;
    Status status = options_.env->GetChildren(dbname_, &filenames);
    if(!status.ok()) {
        return status;
    }

    const bool has_current = options_.env->FileExists(CurrentFileName(dbname_));
    if (has_current) {
        status = versions_.Recover();
        if (!status.ok()) {
            return status;
        }
        last_sequence_ = versions_.LastSequence();
    }

    std::vector<uint64_t> log_numbers;
    for (const std::string& filename : filenames) {
        if (IsDotOrDotDot(filename)) {
            continue;
        }

        uint64_t number = 0;
        FileType type;
        if (!ParseFileName(filename, &number, &type)) {
            continue;
        }

        if (number > 0) {
            versions_.MarkFileNumberUsed(number);
        }

        if (type == kLogFile && (!has_current || number >= versions_.LogNumber())) {
            log_numbers.push_back(number);
        }
    }

    std::sort(log_numbers.begin(), log_numbers.end());
    for (uint64_t log_number : log_numbers) {
        logfile_number_ = log_number;
        status = RecoverLogFile(log_number);
        if (!status.ok()) {
            return status;
        }
    }

    if (!log_numbers.empty() && !mem_->Empty()) {
        imm_ = std::move(mem_);
        mem_ = std::make_unique<MemTable>(internal_comparator_);
        status = FlushMemTable();
        if (!status.ok()) {
            return status;
        }
    }

    const bool replayed_logs = !log_numbers.empty();

    if (has_current || !log_numbers.empty()) {
        logfile_number_ = versions_.NewFileNumber();
    } else {
        logfile_number_ = 1;
    }

    status = InitWAL();
    if (!status.ok()) {
        return status;
    }

    if (replayed_logs) {
        VersionEdit edit;
        edit.SetLogNumber(logfile_number_);
        edit.SetLastSequence(last_sequence_);
        status = versions_.LogAndApply(&edit);
        if (!status.ok()) {
            return status;
        }
    }

    status = RemoveObsoleteFiles();
    if (status.ok()) {
        MaybeScheduleCompaction();
    }
    return status;
}

Status DBImpl::RecoverLogFile(uint64_t log_number) {
    const std::string log_name = LogFileName(dbname_, log_number);
    SequentialFile* raw_file = nullptr;
    Status status = options_.env->NewSequentialFile(log_name, &raw_file);
    if (!status.ok()) {
        return status;
    }

    std::unique_ptr<SequentialFile> file(raw_file);
    log::Reader reader(file.get(), true);
    std::string record;
    Status read_status;

    while (reader.ReadRecord(&record, &read_status)) {
        if (record.size() < kWriteBatchHeaderSize) {
            return Status::Corruption("short WriteBatch record in WAL", log_name);
        }

        WriteBatch batch;
        WriteBatchInternal::SetContents(&batch, Slice(record));
        const int count = WriteBatchInternal::Count(&batch);
        if (count < 0) {
            return Status::Corruption("negative WriteBatch count in WAL", log_name);
        }
        if (count == 0) {
            continue;
        }

        const SequenceNumber batch_sequence = WriteBatchInternal::Sequence(&batch);
        const SequenceNumber count_as_sequence = static_cast<SequenceNumber>(count);
        if (batch_sequence > kMaxSequenceNumber - count_as_sequence + 1) {
            return Status::Corruption("WriteBatch sequence overflow in WAL", log_name);
        }

        status = WriteBatchInternal::InsertInto(&batch, mem_.get());
        if (!status.ok()) {
            return status;
        }

        const SequenceNumber batch_last_sequence = batch_sequence + count_as_sequence - 1;
        if (last_sequence_ < batch_last_sequence) {
            last_sequence_ = batch_last_sequence;
        }

        if (mem_->ApproximateMemoryUsage() > options_.write_buffer_size) {
            imm_ = std::move(mem_);
            mem_ = std::make_unique<MemTable>(internal_comparator_);
            status = FlushMemTable();
            if (!status.ok()) {
                return status;
            }
        }
    }

    if (!read_status.ok()) {
        return read_status;
    }

    return Status::OK();
}

Status DBImpl::RemoveObsoleteFiles() {
    std::vector<std::string> filenames;
    Status status = options_.env->GetChildren(dbname_, &filenames);
    if (!status.ok()) {
        return status;
    }

    // 保存当前 Version 仍然引用的 SSTable 文件号
    std::set<uint64_t> live_tables;
    versions_.AddLiveFiles(&live_tables);
    // compaction output 安装前不在 VersionSet 中，临时视为 live table，避免误删
    for (uint64_t file_number : pending_outputs_) {
        live_tables.insert(file_number);
    }

    // 保存CURRENT 当前指向的 MANIFEST 文件号，0表示没有 MANIFEST
    uint64_t current_manifest_number = 0;
    if (options_.env->FileExists(CurrentFileName(dbname_))) {
        std::string current;
        status = ReadFileToString(options_.env, CurrentFileName(dbname_), &current);
        if (!status.ok()) {
            return status;
        }
        if (!current.empty() && current.back() == '\n') {
            current.pop_back();
        }
        FileType current_type;
        if (!ParseFileName(current, &current_manifest_number, &current_type) || current_type != kDescriptorFile) {
            return Status::Corruption("CURRENT points to invalid MANIFEST", current);
        }
    }

    // 文件名解析判断
    for (const std::string& filename : filenames) {
        if (IsDotOrDotDot(filename)) {
            continue;
        }

        uint64_t number = 0;
        FileType type;
        if (!ParseFileName(filename, &number, &type)) {
            continue;
        }

        bool keep = true;
        switch (type) {
        case kTableFile:
            keep = live_tables.count(number) != 0;
            break;
        case kDescriptorFile:
            keep = number == current_manifest_number;
            break;
        case kLogFile:
            keep = current_manifest_number == 0 || versions_.LogNumber() == 0 || number >= versions_.LogNumber() || number == logfile_number_;
            break;
        case kTempFile:
            keep = false;
            break;
        case kCurrentFile:
        case kDBLockFile:
        case kInfoLogFile:
            keep = true;
            break;
        }

        if (!keep) {
            const std::string path = dbname_ + "/" + filename;
            if (options_.env->FileExists(path)) {
                status = options_.env->RemoveFile(path);
                if (!status.ok()) {
                    return status;
                }
            }
        }
    }

    return Status::OK();
}

Status DB::Open(const Options& options, const std::string& dbname, DB** dbptr) {
    if (dbptr == nullptr) {
        return Status::InvalidArgument("DB::Open dbptr is null");
    }

    *dbptr = nullptr;

    if (options.comparator == nullptr) {
        return Status::InvalidArgument("DB::Open comparator is null");
    }

    DBImpl* impl = new DBImpl(options, dbname);

    Status s = impl->Open();
    if (!s.ok()) {
        delete impl;
        return s;
    }

    *dbptr = impl;
    return Status::OK();
}

Status DBImpl::MakeRoomForWrite() {
    if (imm_ != nullptr) {
        return FlushMemTable();
    }

    if (mem_->ApproximateMemoryUsage() <= options_.write_buffer_size) {
        return Status::OK();
    }

    imm_ = std::move(mem_);
    mem_ = std::make_unique<MemTable>(internal_comparator_);
    return FlushMemTable();
}

Status DBImpl::FlushMemTable() {
    if (imm_ == nullptr) {
        return Status::OK();
    }

    FileMetaData meta;
    meta.number = versions_.NewFileNumber();

    std::unique_ptr<Iterator> iter(imm_->NewIterator());
    Status status = BuildTable(dbname_, options_.env, table_options_, iter.get(), &meta);
    if (!status.ok()) {
        return status;
    }

    if (meta.file_size > 0) {
        VersionEdit edit;
        edit.SetLogNumber(logfile_number_);
        edit.SetLastSequence(last_sequence_);
        edit.AddFile(0, meta.number, meta.file_size, meta.smallest, meta.largest);
        status = versions_.LogAndApply(&edit);
        if (!status.ok()) {
            (void)options_.env->RemoveFile(TableFileName(dbname_, meta.number));
            return status;
        }
    }

    imm_.reset();
    return Status::OK();
}

Status DBImpl::CompactMemTable() {
    if (imm_ != nullptr) {
        return FlushMemTable();
    }
    if (mem_ == nullptr || mem_->Empty()) {
        return Status::OK();
    }
    imm_ = std::move(mem_);
    mem_ = std::make_unique<MemTable>(internal_comparator_);
    return FlushMemTable();
}

void DBImpl::RecordBackgroundError(const Status& status) {
    if (status.ok()) {
        return;
    }
    std::lock_guard<std::mutex> lock(background_mu_);
    if (bg_error_.ok()) {
        bg_error_ = status;
    }
}

void DBImpl::MaybeScheduleCompaction() {
    bool should_schedule = false;
    // 开启局部锁作用域，避免调用外部 Env
    {
        std::lock_guard<std::mutex> lock(background_mu_);
        if (background_compaction_scheduled_) {
            return;
        }
        if (shutting_down_) {
            return;
        }
        if (!bg_error_.ok()) {
            return;
        }
        if (imm_ == nullptr && !versions_.NeedsCompaction()) {
            return;
        }
        background_compaction_scheduled_ = true;
        should_schedule = true;
    }
    if (should_schedule) {
        options_.env->Schedule(&DBImpl::BGWork, this);
    }
}

void DBImpl::BGWork(void* db) {
    reinterpret_cast<DBImpl*>(db)->BackgroundCall();
}

void DBImpl::BackgroundCall() {
    bool should_run = false;
    {
        std::lock_guard<std::mutex> lock(background_mu_);
        background_compaction_running_ = true;
        should_run = !shutting_down_ && bg_error_.ok();
    }
    Status status = Status::OK();
    if (should_run) {
        status = BackgroundCompaction();
    }
    if (!status.ok()) {
        RecordBackgroundError(status);
    }
    {
        std::lock_guard<std::mutex> lock(background_mu_);
        background_compaction_running_ = false;
        background_compaction_scheduled_ = false;
    }
    MaybeScheduleCompaction();
    // 唤醒析构或测试等待者
    background_cv_.notify_all();
}

Status DBImpl::BackgroundCompaction() {
    if (imm_ != nullptr) {
        return CompactMemTable();
    }

    std::unique_ptr<Compaction> compaction(versions_.PickCompaction());
    if (compaction == nullptr) {
        return Status::OK();
    }
    // CompactionState* compact = new CompactionState(compaction.get());
    // Status status = DoCompactionWork(compact);
    // CleanupCompaction(compact);
    return RunCompaction(compaction.get());
}

Status DBImpl::RunCompaction(Compaction* compaction) {
    assert(compaction != nullptr);
    if (compaction->IsTrivialMove()) {
        const FileMetaData* file = compaction->input(0, 0);
        VersionEdit* edit = compaction->edit();
        edit->RemoveFile(compaction->level(), file->number);
        edit->AddFile(compaction->level() + 1, file->number, file->file_size, file->smallest, file->largest);
        Status status = versions_.LogAndApply(edit);
        if (status.ok()) {
            compaction->ReleaseInputs();
            status = RemoveObsoleteFiles();
        }
        return status;
    }
    CompactionState* compact = new CompactionState(compaction);
    Status status = DoCompactionWork(compact);
    CleanupCompaction(compact);
    if (!status.ok()) {
        (void)RemoveObsoleteFiles();
    }
    return status;
}

Status DBImpl::TEST_CompactMemTable() {
    return CompactMemTable();
}

Status DBImpl::TEST_CompactRange(int level, const Slice* begin, const Slice* end) {
    if (level < 0 || level + 1 >= kNumLevels) {
        return Status::InvalidArgument("TEST_CompactRange level out of range");
    }
    InternalKey begin_storage;
    InternalKey end_storage;
    const InternalKey* begin_key = nullptr;
    const InternalKey* end_key = nullptr;
    if (begin != nullptr) {
        begin_storage = InternalKey(*begin, kMaxSequenceNumber, kValueTypeForSeek);
        begin_key = &begin_storage;
    }
    if (end != nullptr) {
        end_storage = InternalKey(*end, 0, static_cast<ValueType>(0));
        end_key = &end_storage;
    }
    Status status = Status::OK();
    while (status.ok()) {
        std::unique_ptr<Compaction> compaction(versions_.CompactRange(level, begin_key, end_key));
        if (compaction == nullptr) {
            break;
        }
        status = RunCompaction(compaction.get());
    }
    if (!status.ok()) {
        RecordBackgroundError(status);
    }
    return status;
}

void DBImpl::CleanupCompaction(CompactionState* compact) {
    if (compact == nullptr) {
        return;
    }

    if (compact->builder != nullptr) {
        compact->builder->Abandon();
        delete compact->builder;
        compact->builder = nullptr;
    }

    if (compact->outfile != nullptr) {
        (void)compact->outfile->Close();
        delete compact->outfile;
        compact->outfile = nullptr;
    }

    for (const CompactionState::Output& output : compact->outputs) {
        pending_outputs_.erase(output.number);
        if (output.file_size == 0) {
            (void)options_.env->RemoveFile(TableFileName(dbname_, output.number));
        }
    }

    delete compact;
}

Status DBImpl::OpenCompactionOutputFile(CompactionState* compact) {
    assert(compact != nullptr);
    assert(compact->builder == nullptr);
    assert(compact->outfile == nullptr);

    const uint64_t file_number = versions_.NewFileNumber();
    pending_outputs_.insert(file_number);

    CompactionState::Output output;
    output.number = file_number;
    output.file_size = 0;
    output.smallest.Clear();
    output.largest.Clear();
    compact->outputs.push_back(output);

    const std::string filename = TableFileName(dbname_, file_number);
    Status status = options_.env->NewWritableFile(filename, &compact->outfile);
    if (!status.ok()) {
        pending_outputs_.erase(file_number);
        compact->outputs.pop_back();
        return status;
    }

    compact->builder = new TableBuilder(table_options_, compact->outfile);
    return Status::OK();
}

Status DBImpl::FinishCOmpactionOutputFile(CompactionState* compact, Iterator* input) {
    assert(compact != nullptr);
    assert(input != nullptr);
    assert(compact->outfile != nullptr);
    assert(compact->builder != nullptr);
    assert(!compact->outputs.empty());

    CompactionState::Output* output = compact->current_output();
    const uint64_t output_number = output->number;
    const uint64_t entry_count = compact->builder->NumEntries();
    Status status = input->status();

    if (status.ok()) {
        status = compact->builder->Finish();
    } else {
        compact->builder->Abandon();
    }

    const uint64_t file_size = compact->builder->FileSize();
    output->file_size = file_size;
    compact->total_bytes += file_size;

    delete compact->builder;
    compact->builder = nullptr;

    if (status.ok()) {
        status = compact->outfile->Sync();
    }
    if (status.ok()) {
        status = compact->outfile->Close();
    } else {
        (void)compact->outfile->Close();
    }

    delete compact->outfile;
    compact->outfile = nullptr;

    if (status.ok() && entry_count > 0) {
        std::unique_ptr<Iterator> iter(table_cache_.NewIterator(ReadOptions(), output_number, file_size));
        iter->SeekToFirst();
        status = iter->status();
    }

    if (!status.ok()) {
        pending_outputs_.erase(output_number);
        (void)options_.env->RemoveFile(TableFileName(dbname_, output_number));
    }

    return status;
}

Status DBImpl::InstallCompactionResults(CompactionState* compact) {
    assert(compact != nullptr);
    assert(compact->compaction != nullptr);
    VersionEdit* edit = compact->compaction->edit();
    compact->compaction->AddInputDeletions(edit);
    const int output_level = compact->compaction->level() + 1;
    for (const CompactionState::Output& output : compact->outputs) {
        if (output.file_size == 0) {
            continue;
        }
        edit->AddFile(output_level, output.number, output.file_size, output.smallest, output.largest);
    }
    return versions_.LogAndApply(edit);
}

Status DBImpl::DoCompactionWork(CompactionState* compact) {
    assert(compact != nullptr);
    assert(compact->compaction != nullptr);
    assert(compact->builder == nullptr);
    assert(compact->outfile == nullptr);

    compact->smallest_snapshot = SmallestSnapshot();
    std::unique_ptr<Iterator> input(versions_.MakeInputIterator(compact->compaction));
    input->SeekToFirst();

    Status status;
    ParsedInternalKey parsed_key;
    std::string current_user_key;
    // 标记 current_user_key 是否已经初始化
    bool has_current_user_key = false;
    SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
    const Comparator* user_comparator = internal_comparator_.user_comparator();

    while (input->Valid()) {
        const Slice key = input->key();

        // 进行切分
        if (compact->compaction->ShouldStopBefore(key) && compact->builder != nullptr) {
            status = FinishCOmpactionOutputFile(compact, input.get());
            if (!status.ok()) {
                break;
            }
        }

        // 推进 grandparent overlap 状态
        bool drop = false;
        if (!ParseInternalKey(key, &parsed_key)) {
            current_user_key.clear();
            has_current_user_key = false;
            last_sequence_for_key = kMaxSequenceNumber;
        } else {
            if (!has_current_user_key ||
                user_comparator->Compare(parsed_key.user_key, Slice(current_user_key)) != 0) {
                current_user_key.assign(parsed_key.user_key.data(), parsed_key.user_key.size());
                has_current_user_key = true;
                last_sequence_for_key = kMaxSequenceNumber;
            }
            if (last_sequence_for_key <= compact->smallest_snapshot) {
                drop = true;
            } else if (parsed_key.type == kTypeDeletion &&
                       parsed_key.sequence <= compact->smallest_snapshot &&
                       compact->compaction->IsBaseLevelForKey(parsed_key.user_key)) {
                drop = true;
            }
            last_sequence_for_key = parsed_key.sequence;
        }

        if (!drop) {
            if (compact->builder == nullptr) {
                status = OpenCompactionOutputFile(compact);
                if (!status.ok()) {
                    break;
                }
            }

            if (compact->builder->NumEntries() == 0) {
                compact->current_output()->smallest.DecodeFrom(key);
            }
            compact->current_output()->largest.DecodeFrom(key);
            compact->builder->Add(key, input->value());

            if (compact->builder->FileSize() >= compact->compaction->MaxOutputFileSize()) {
                status = FinishCOmpactionOutputFile(compact, input.get());
                if (!status.ok()) {
                    break;
                }
            }
        }

        input->Next();
    }

    if (status.ok() && compact->builder != nullptr) {
        status = FinishCOmpactionOutputFile(compact, input.get());
    }

    if (status.ok()) {
        status = input->status();
    }

    if (status.ok()) {
        status = InstallCompactionResults(compact);
    }

    if (status.ok()) {
        compact->compaction->ReleaseInputs();
        status = RemoveObsoleteFiles();
    }

    return status;
}

}
