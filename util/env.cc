#include <lindb/env.h>
#include <lindb/slice.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace lindb {

// Env 基类析构函数，保证通过基类指针释放派生类对象时行为正确。
Env::~Env() = default;

// 默认不支持追加写文件，调用方需要能处理 NotSupported。
Status Env::NewAppendableFile(const std::string& fname, WritableFile** result) {
    if (result != nullptr) {
        *result = nullptr;
    }
    return Status::NotSupported("NewAppendableFile", fname);
}

// 兼容旧接口，默认转发到 DeleteFile。
Status Env::RemoveFile(const std::string& fname) {
    return DeleteFile(fname);
}

// 兼容旧接口，默认转发到 RemoveFile。
Status Env::DeleteFile(const std::string& fname) {
    return RemoveFile(fname);
}

// 兼容旧接口，默认转发到 DeleteDir。
Status Env::RemoveDir(const std::string& dirname) {
    return DeleteDir(dirname);
}

// 兼容旧接口，默认转发到 RemoveDir。
Status Env::DeleteDir(const std::string& dirname) {
    return RemoveDir(dirname);
}

// SequentialFile 基类虚析构，支持通过接口安全析构。
SequentialFile::~SequentialFile() = default;

// RandomAccessFile 基类虚析构，支持通过接口安全析构。
RandomAccessFile::~RandomAccessFile() = default;

// WritableFile 基类虚析构，支持通过接口安全析构。
WritableFile::~WritableFile() = default;

// Logger 基类虚析构，支持通过接口安全析构。
Logger::~Logger() = default;

// FileLock 基类虚析构，支持通过接口安全析构。
FileLock::~FileLock() = default;

// EnvWrapper 析构函数，包装器本身不拥有 target_ 生命周期。
EnvWrapper::~EnvWrapper() = default;

// 对非空 Logger 执行一次 printf 风格日志写入。
void Log(Logger* info_log, const char* format, ...) {
    if (info_log == nullptr) {
        return;
    }

    std::va_list ap;
    va_start(ap, format);
    info_log->Logv(format, ap);
    va_end(ap);
}

namespace {

// 只读文件句柄数量上限，首次按系统资源限制惰性初始化。
int g_open_read_only_file_limit = -1;

// 后台随机读文件允许保留的只读 fd 数量默认值。
constexpr int kDefaultReadOnlyFileLimit = 50;

// WritableFile 内部顺序写缓冲大小，减少小块 write 系统调用。
constexpr size_t kWritableFileBufferSize = 65536;

// 把 POSIX errno 映射成 lindb::Status。
Status PosixError(const std::string& context, int error_number) {
    if (error_number == ENOENT) {
        return Status::NotFound(context, std::strerror(error_number));
    }
    return Status::IOError(context, std::strerror(error_number));
}

// 对 fd 执行非阻塞加锁或解锁，供 DB 进程锁实现复用。
int LockOrUnlock(int fd, bool lock) {
    struct ::flock file_lock;
    std::memset(&file_lock, 0, sizeof(file_lock));
    file_lock.l_type = lock ? F_WRLCK : F_UNLCK;
    file_lock.l_whence = SEEK_SET;
    file_lock.l_start = 0;
    file_lock.l_len = 0;
    return ::fcntl(fd, F_SETLK, &file_lock);
}

// 把一段字符串完整写入文件，可选在 close 前执行一次 sync。
Status DoWriteStringToFile(Env* env, const Slice& data, const std::string& fname,
                           bool should_sync) {
    WritableFile* file = nullptr;
    Status status = env->NewWritableFile(fname, &file);
    if (!status.ok()) {
        return status;
    }

    status = file->Append(data);
    if (status.ok() && should_sync) {
        status = file->Sync();
    }
    if (status.ok()) {
        status = file->Close();
    }

    delete file;
    if (!status.ok()) {
        env->RemoveFile(fname);
    }
    return status;
}

// 顺序读取的 POSIX 文件实现，内部维护单个可推进的 fd 偏移。
class PosixSequentialFile final : public SequentialFile {
public:
    // 接管已经打开的 fd，并记录文件名用于错误报告。
    PosixSequentialFile(std::string filename, int fd)
        : filename_(std::move(filename)), fd_(fd) {}

    // 析构时关闭底层 fd，避免句柄泄漏。
    ~PosixSequentialFile() override { ::close(fd_); }

    // 从当前位置顺序读取最多 n 字节，并把结果映射到 scratch。
    Status Read(size_t n, Slice* result, char* scratch) override {
        while (true) {
            const ssize_t read_size = ::read(fd_, scratch, n);
            if (read_size < 0) {
                if (errno == EINTR) {
                    continue;
                }
                *result = Slice();
                return PosixError(filename_, errno);
            }
            *result = Slice(scratch, static_cast<size_t>(read_size));
            return Status::OK();
        }
    }

    // 通过 lseek 跳过 n 字节，失败时返回对应 IO 错误。
    Status Skip(uint64_t n) override {
        if (n > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
            return Status::InvalidArgument("skip offset too large", filename_);
        }
        if (::lseek(fd_, static_cast<off_t>(n), SEEK_CUR) == static_cast<off_t>(-1)) {
            return PosixError(filename_, errno);
        }
        return Status::OK();
    }

private:
    std::string filename_;
    int fd_;
};

// 随机读取的 POSIX 文件实现，使用 pread 保证并发读互不影响偏移。
class PosixRandomAccessFile final : public RandomAccessFile {
public:
    // 接管已经打开的只读 fd，并记录文件名用于错误报告。
    PosixRandomAccessFile(std::string filename, int fd)
        : filename_(std::move(filename)), fd_(fd) {}

    // 析构时关闭底层 fd，避免句柄泄漏。
    ~PosixRandomAccessFile() override { ::close(fd_); }

    // 从给定 offset 读取最多 n 字节到 scratch，不修改共享文件偏移。
    Status Read(uint64_t offset, size_t n, Slice* result, char* scratch) const override {
        if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
            *result = Slice();
            return Status::InvalidArgument("read offset too large", filename_);
        }

        while (true) {
            const ssize_t read_size = ::pread(fd_, scratch, n, static_cast<off_t>(offset));
            if (read_size < 0) {
                if (errno == EINTR) {
                    continue;
                }
                *result = Slice();
                return PosixError(filename_, errno);
            }
            *result = Slice(scratch, static_cast<size_t>(read_size));
            return Status::OK();
        }
    }

private:
    std::string filename_;
    int fd_;
};

// 线程安全日志实现，把格式化内容按行追加到对应文件。
class PosixLogger final : public Logger {
public:
    // 接管 FILE*，后续析构时负责 fclose。
    explicit PosixLogger(std::FILE* file) : file_(file) {}

    // 析构时关闭日志文件，确保缓冲落盘。
    ~PosixLogger() override {
        if (file_ != nullptr) {
            std::fclose(file_);
        }
    }

    // 在互斥保护下完成一次格式化写入，避免多线程日志交叉。
    void Logv(const char* format, std::va_list ap) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vfprintf(file_, format, ap);
        std::fputc('\n', file_);
        std::fflush(file_);
    }

private:
    std::mutex mutex_;
    std::FILE* file_;
};

// 顺序写文件实现，内部使用固定缓冲减少频繁小写调用。
class PosixWritableFile final : public WritableFile {
public:
    // 接管一个可写 fd，并记录文件名用于错误报告。
    PosixWritableFile(std::string filename, int fd)
        : filename_(std::move(filename)), fd_(fd), pos_(0) {}

    // 析构时若调用方未显式 Close，则补做一次关闭。
    ~PosixWritableFile() override {
        if (fd_ >= 0) {
            Close();
        }
    }

    // 先写入内部缓冲，缓冲装不下时再刷到底层 fd。
    Status Append(const Slice& data) override {
        size_t write_size = data.size();
        const char* write_data = data.data();

        if (write_size <= kWritableFileBufferSize - pos_) {
            std::memcpy(buf_ + pos_, write_data, write_size);
            pos_ += write_size;
            return Status::OK();
        }

        const size_t copy_size = kWritableFileBufferSize - pos_;
        if (copy_size > 0) {
            std::memcpy(buf_ + pos_, write_data, copy_size);
            pos_ += copy_size;
            write_data += copy_size;
            write_size -= copy_size;
        }

        Status status = FlushBuffer();
        if (!status.ok()) {
            return status;
        }

        if (write_size < kWritableFileBufferSize) {
            std::memcpy(buf_, write_data, write_size);
            pos_ = write_size;
            return Status::OK();
        }
        return WriteUnbuffered(write_data, write_size);
    }

    // 先刷新缓冲，再关闭 fd。
    Status Close() override {
        Status status = FlushBuffer();
        if (::close(fd_) != 0 && status.ok()) {
            status = PosixError(filename_, errno);
        }
        fd_ = -1;
        return status;
    }

    // 只把用户态缓冲写到内核，不强制持久化到磁盘。
    Status Flush() override {
        return FlushBuffer();
    }

    // 先刷新缓冲，再通过 fsync 强制持久化到底层存储。
    Status Sync() override {
        Status status = FlushBuffer();
        if (!status.ok()) {
            return status;
        }
        if (::fsync(fd_) != 0) {
            return PosixError(filename_, errno);
        }
        return Status::OK();
    }

private:
    // 把当前缓冲区的有效内容写出，并清空缓冲位置。
    Status FlushBuffer() {
        const Status status = WriteUnbuffered(buf_, pos_);
        if (status.ok()) {
            pos_ = 0;
        }
        return status;
    }

    // 循环调用 write，直到把指定数据全部写完。
    Status WriteUnbuffered(const char* data, size_t size) {
        while (size > 0) {
            const ssize_t write_size = ::write(fd_, data, size);
            if (write_size < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return PosixError(filename_, errno);
            }
            if (write_size == 0) {
                return Status::IOError(filename_, "write returned 0");
            }
            data += write_size;
            size -= static_cast<size_t>(write_size);
        }
        return Status::OK();
    }

    std::string filename_;
    int fd_;
    size_t pos_;
    char buf_[kWritableFileBufferSize];
};

// 保存一次成功文件锁的信息，供后续解锁和关闭 fd 使用。
class PosixFileLock final : public FileLock {
public:
    // 记录加锁成功的 fd 和文件名。
    PosixFileLock(int fd, std::string filename)
        : fd_(fd), filename_(std::move(filename)) {}

    // 返回持有锁的底层 fd。
    int fd() const { return fd_; }

    // 返回锁文件路径，用于错误信息和进程内锁表管理。
    const std::string& filename() const { return filename_; }

private:
    int fd_;
    std::string filename_;
};

// 维护当前进程内已经持有的锁文件集合，避免重复锁同一路径。
class PosixLockTable {
public:
    // 尝试登记一个锁文件，成功返回 true，重复登记返回 false。
    bool Insert(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        return locked_files_.insert(filename).second;
    }

    // 从进程内锁表中移除一个已经释放的文件锁。
    void Remove(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        locked_files_.erase(filename);
    }

private:
    std::mutex mutex_;
    std::set<std::string> locked_files_;
};

// 默认 POSIX Env，实现文件系统、锁、线程和后台任务抽象。
class PosixEnv final : public Env {
public:
    // 初始化后台线程状态和进程内锁表。
    PosixEnv() : started_background_thread_(false) {}

    // 打开顺序读文件，成功后返回 PosixSequentialFile。
    Status NewSequentialFile(const std::string& fname, SequentialFile** result) override {
        *result = nullptr;
        const int fd = ::open(fname.c_str(), O_RDONLY);
        if (fd < 0) {
            return PosixError(fname, errno);
        }
        *result = new PosixSequentialFile(fname, fd);
        return Status::OK();
    }

    // 打开随机读文件，成功后返回基于 pread 的随机访问实现。
    Status NewRandomAccessFile(const std::string& fname, RandomAccessFile** result) override {
        *result = nullptr;
        const int fd = ::open(fname.c_str(), O_RDONLY);
        if (fd < 0) {
            return PosixError(fname, errno);
        }
        *result = new PosixRandomAccessFile(fname, fd);
        return Status::OK();
    }

    // 以截断重建方式打开可写文件。
    Status NewWritableFile(const std::string& fname, WritableFile** result) override {
        *result = nullptr;
        const int fd = ::open(fname.c_str(), O_TRUNC | O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            return PosixError(fname, errno);
        }
        *result = new PosixWritableFile(fname, fd);
        return Status::OK();
    }

    // 以追加方式打开可写文件，不存在时自动创建。
    Status NewAppendableFile(const std::string& fname, WritableFile** result) override {
        *result = nullptr;
        const int fd = ::open(fname.c_str(), O_APPEND | O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            return PosixError(fname, errno);
        }
        *result = new PosixWritableFile(fname, fd);
        return Status::OK();
    }

    // 判断路径是否存在。
    bool FileExists(const std::string& fname) override {
        return ::access(fname.c_str(), F_OK) == 0;
    }

    // 读取目录下所有子项名字，结果为相对目录名。
    Status GetChildren(const std::string& dir, std::vector<std::string>* result) override {
        result->clear();
        ::DIR* directory = ::opendir(dir.c_str());
        if (directory == nullptr) {
            return PosixError(dir, errno);
        }

        while (true) {
            errno = 0;
            struct ::dirent* entry = ::readdir(directory);
            if (entry == nullptr) {
                break;
            }
            result->emplace_back(entry->d_name);
        }

        const int saved_errno = errno;
        ::closedir(directory);
        if (saved_errno != 0) {
            return PosixError(dir, saved_errno);
        }
        return Status::OK();
    }

    // 删除普通文件。
    Status RemoveFile(const std::string& fname) override {
        if (::unlink(fname.c_str()) != 0) {
            return PosixError(fname, errno);
        }
        return Status::OK();
    }

    // 创建目录。
    Status CreateDir(const std::string& dirname) override {
        if (::mkdir(dirname.c_str(), 0755) != 0) {
            return PosixError(dirname, errno);
        }
        return Status::OK();
    }

    // 删除空目录。
    Status RemoveDir(const std::string& dirname) override {
        if (::rmdir(dirname.c_str()) != 0) {
            return PosixError(dirname, errno);
        }
        return Status::OK();
    }

    // 查询文件大小并写回到 file_size。
    Status GetFileSize(const std::string& fname, uint64_t* file_size) override {
        struct ::stat file_stat;
        if (::stat(fname.c_str(), &file_stat) != 0) {
            *file_size = 0;
            return PosixError(fname, errno);
        }
        if (file_stat.st_size < 0) {
            *file_size = 0;
            return PosixError(fname, EINVAL);
        }
        *file_size = static_cast<uint64_t>(file_stat.st_size);
        return Status::OK();
    }

    // 原子重命名文件路径。
    Status RenameFile(const std::string& src, const std::string& target) override {
        if (::rename(src.c_str(), target.c_str()) != 0) {
            return PosixError(src, errno);
        }
        return Status::OK();
    }

    // 尝试获取 DB 文件锁，失败立即返回，不阻塞等待。
    Status LockFile(const std::string& fname, FileLock** lock) override {
        *lock = nullptr;

        const int fd = ::open(fname.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd < 0) {
            return PosixError(fname, errno);
        }

        if (!locks_.Insert(fname)) {
            ::close(fd);
            return Status::IOError("lock " + fname, "already held by process");
        }

        if (LockOrUnlock(fd, true) == -1) {
            const int lock_errno = errno;
            ::close(fd);
            locks_.Remove(fname);
            return PosixError("lock " + fname, lock_errno);
        }

        *lock = new PosixFileLock(fd, fname);
        return Status::OK();
    }

    // 释放先前成功获取的文件锁，并关闭对应 fd。
    Status UnlockFile(FileLock* lock) override {
        auto* posix_lock = static_cast<PosixFileLock*>(lock);
        if (LockOrUnlock(posix_lock->fd(), false) == -1) {
            return PosixError("unlock " + posix_lock->filename(), errno);
        }

        locks_.Remove(posix_lock->filename());
        ::close(posix_lock->fd());
        delete posix_lock;
        return Status::OK();
    }

    // 把任务放入后台队列，必要时懒启动一个常驻后台线程。
    void Schedule(void (*function)(void* arg), void* arg) override {
        std::unique_lock<std::mutex> lock(background_work_mutex_);
        if (!started_background_thread_) {
            started_background_thread_ = true;
            std::thread background_thread(&PosixEnv::BackgroundThreadMain, this);
            background_thread.detach();
        }
        background_work_queue_.emplace(function, arg);
        lock.unlock();
        background_work_cv_.notify_one();
    }

    // 启动一个独立线程执行给定函数，线程结束后自动回收。
    void StartThread(void (*function)(void* arg), void* arg) override {
        std::thread new_thread(function, arg);
        new_thread.detach();
    }

    // 返回可用于测试的临时目录，并确保目录存在。
    Status GetTestDirectory(std::string* path) override {
        const char* env = std::getenv("TEST_TMPDIR");
        if (env != nullptr && env[0] != '\0') {
            *path = env;
        } else {
            char buffer[100];
            std::snprintf(buffer, sizeof(buffer), "/tmp/lindbtest-%d",
                          static_cast<int>(::geteuid()));
            *path = buffer;
        }

        if (::mkdir(path->c_str(), 0755) != 0 && errno != EEXIST) {
            return PosixError(*path, errno);
        }
        return Status::OK();
    }

    // 以追加模式打开日志文件，并返回线程安全 Logger。
    Status NewLogger(const std::string& fname, Logger** result) override {
        *result = nullptr;
        const int fd = ::open(fname.c_str(), O_APPEND | O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            return PosixError(fname, errno);
        }

        std::FILE* file = ::fdopen(fd, "a");
        if (file == nullptr) {
            ::close(fd);
            return PosixError(fname, errno);
        }

        *result = new PosixLogger(file);
        return Status::OK();
    }

    // 返回当前时间点的微秒时间戳，用于统计耗时或生成临时名。
    uint64_t NowMicros() override {
        constexpr uint64_t kUsecondsPerSecond = 1000000;
        struct ::timeval tv;
        ::gettimeofday(&tv, nullptr);
        return static_cast<uint64_t>(tv.tv_sec) * kUsecondsPerSecond +
               static_cast<uint64_t>(tv.tv_usec);
    }

    // 让当前线程休眠指定微秒数。
    void SleepForMicroseconds(int micros) override {
        if (micros <= 0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(micros));
    }

private:
    // 后台队列中的一个任务单元，保存函数指针和参数。
    struct BackgroundWorkItem {
        // 构造一个待执行后台任务。
        BackgroundWorkItem(void (*function)(void* arg), void* arg)
            : function(function), arg(arg) {}

        void (*function)(void*);
        void* arg;
    };

    // 后台线程主循环，持续从队列中取任务并执行。
    void BackgroundThreadMain() {
        while (true) {
            BackgroundWorkItem item(nullptr, nullptr);
            {
                std::unique_lock<std::mutex> lock(background_work_mutex_);
                background_work_cv_.wait(lock,
                                         [this] { return !background_work_queue_.empty(); });
                item = background_work_queue_.front();
                background_work_queue_.pop();
            }
            item.function(item.arg);
        }
    }

    std::mutex background_work_mutex_;
    std::condition_variable background_work_cv_;
    bool started_background_thread_;
    std::queue<BackgroundWorkItem> background_work_queue_;
    PosixLockTable locks_;
};

}  // namespace

// 把整个字符串写入目标文件，不额外执行 fsync。
Status WriteStringToFile(Env* env, const Slice& data, const std::string& fname) {
    return DoWriteStringToFile(env, data, fname, false);
}

Status WriteStringToFileSync(Env* env, const Slice& data,const std::string& fname) {
    return DoWriteStringToFile(env, data, fname, true);
}

// 顺序读取整个文件内容并追加到目标字符串中。
Status ReadFileToString(Env* env, const std::string& fname, std::string* data) {
    SequentialFile* file = nullptr;
    Status status = env->NewSequentialFile(fname, &file);
    if (!status.ok()) {
        return status;
    }

    data->clear();
    char scratch[8192];
    Slice fragment;
    while (true) {
        status = file->Read(sizeof(scratch), &fragment, scratch);
        if (!status.ok()) {
            break;
        }
        data->append(fragment.data(), fragment.size());
        if (fragment.empty()) {
            break;
        }
    }

    delete file;
    return status;
}

// 返回当前平台默认的 POSIX Env 单例。
Env* Env::Default() {
    static PosixEnv* default_env = new PosixEnv();
    return default_env;
}

}  // namespace lindb
