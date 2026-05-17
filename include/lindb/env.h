#pragma once 

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <lindb/status.h>

namespace lindb {

class Slice;
class SequentialFile;
class RandomAccessFile;
class WritableFile;
class Logger;
class FileLock;

// 定义系统环境抽象，屏蔽具体 OS 文件系统和线程 API
class Env {
public:
    Env() = default;
    virtual ~Env();
    Env(const Env&) = delete;
    Env& operator=(const Env&) = delete;

    static Env* Default();

    virtual Status NewSequentialFile(const std::string& fname, SequentialFile** result) = 0;
    virtual Status NewRandomAccessFile(const std::string& fname, RandomAccessFile** result) = 0;
    virtual Status NewWritableFile(const std::string& fname, WritableFile** result) = 0;
    virtual Status NewAppendableFile(const std::string& fname, WritableFile** result) = 0;
    virtual bool FileExists(const std::string& fname) = 0;
    virtual Status GetChildren(const std::string& dir, std::vector<std::string>* result) = 0;
    virtual Status RemoveFile(const std::string& fname);
    virtual Status DeleteFile(const std::string& fname);
    virtual Status CreateDir(const std::string& dirname) = 0;
    virtual Status RemoveDir(const std::string& dirname);
    virtual Status DeleteDir(const std::string& dirname);
    virtual Status GetFileSize(const std::string& fname, uint64_t* file_size) = 0;
    virtual Status RenameFile(const std::string& src, const std::string& target) = 0;
    virtual Status LockFile(const std::string& fname, FileLock**) = 0;
    virtual Status UnlockFile(FileLock* lock) = 0;
    // 把任务丢到后台执行
    virtual void Schedule(void (*function)(void* arg), void* arg) = 0;
    virtual void StartThread(void (*function)(void* arg), void* arg) = 0;
    // 返回测试目录路径
    virtual Status GetTestDirectory(std::string* path) = 0;
    virtual Status NewLogger(const std::string& fname, Logger** result) = 0;
    // 返回当前时间戳
    virtual uint64_t NowMicros() = 0;
    virtual void SleepForMicroseconds(int micros) = 0;
};

class SequentialFile {
public:
    SequentialFile() = default;
    virtual ~SequentialFile();
    SequentialFile(const SequentialFile&) = delete;
    SequentialFile& operator=(const SequentialFile&) = delete;

    virtual Status Read(size_t n, Slice* result, char* scratch) = 0;
    virtual Status Skip(uint64_t n) = 0;
};

class RandomAccessFile {
public:
    RandomAccessFile() = default;
    virtual ~RandomAccessFile();
    RandomAccessFile(const RandomAccessFile&) = delete;
    RandomAccessFile& operator=(const RandomAccessFile&) = delete;

    virtual Status Read(uint64_t offset, size_t n, Slice* result, char* scratch) const = 0;
};

class WritableFile {
public:
    WritableFile() = default;
    virtual ~WritableFile();
    WritableFile(const WritableFile&) = delete;
    WritableFile& operator=(const WritableFile&) = delete;

    virtual Status Append(const Slice& data) = 0;
    virtual Status Close() = 0;
    // 刷新用户态缓冲
    virtual Status Flush() = 0;
    virtual Status Sync() = 0;
};

class Logger {
public:
    Logger() = default;
    virtual ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 以 printf 风格写一条日志
    virtual void Logv(const char* format, std::va_list ap) = 0;
};

class FileLock {
public:
    FileLock() = default;
    virtual ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
};

void Log(Logger* info_log, const char* format, ...);
Status WriteStringToFile(Env* env, const Slice& data, const std::string& fname);
Status WriteStringToFileSync(Env* env, const Slice& data, const std::string& fname);
Status ReadFileToString(Env* env, const std::string& fname, std::string* data);

class EnvWrapper : public Env {
public:
    explicit EnvWrapper(Env* target) : target_(target) {}
    ~EnvWrapper() override;
    Env* target() const { return target_; }

    Status NewSequentialFile(const std::string& f, SequentialFile** r) override {
        return target_->NewSequentialFile(f, r);
    }

    Status NewRandomAccessFile(const std::string& f, RandomAccessFile** r) override {
        return target_->NewRandomAccessFile(f, r);
    }

    Status NewWritableFile(const std::string& f, WritableFile** r) override {
        return target_->NewWritableFile(f, r);
    }

    Status NewAppendableFile(const std::string& f, WritableFile** r) override {
        return target_->NewAppendableFile(f, r);
    }

    bool FileExists(const std::string& f) override {
        return target_->FileExists(f);
    }

    Status GetChildren(const std::string& dir, std::vector<std::string>* r) override {
        return target_->GetChildren(dir, r);
    }

    Status RemoveFile(const std::string& f) override {
        return target_->RemoveFile(f);
    }

    Status CreateDir(const std::string& d) override {
        return target_->CreateDir(d);
    }

    Status RemoveDir(const std::string& d) override {
        return target_->RemoveDir(d);
    }

    Status GetFileSize(const std::string& f, uint64_t* s) override {
        return target_->GetFileSize(f, s);
    }

    Status RenameFile(const std::string& s, const std::string& t) override {
        return target_->RenameFile(s, t);
    }

    Status LockFile(const std::string& f, FileLock** l) override {
        return target_->LockFile(f, l);
    }

    Status UnlockFile(FileLock* l) override {
        return target_->UnlockFile(l);
    }

    void Schedule(void (*f)(void*), void* a) override {
        target_->Schedule(f, a);
    }

    void StartThread(void (*f)(void*), void* a) override {
        target_->StartThread(f, a);
    }

    Status GetTestDirectory(std::string* path) override {
        return target_->GetTestDirectory(path);
    }

    Status NewLogger(const std::string& fname, Logger** result) override {
        return target_->NewLogger(fname, result);
    }

    uint64_t NowMicros() override {
        return target_->NowMicros();
    }

    void SleepForMicroseconds(int micros) override {
        target_->SleepForMicroseconds(micros);
    }

private:
    Env* target_;
};

}