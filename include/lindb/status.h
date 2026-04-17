// 定义统一的返回状态类型
#pragma once

#include <string>
#include <lindb/slice.h>

namespace lindb {

class Status {
public:
    Status() noexcept;
    ~Status();

    Status(const Status& rhs);
    Status& operator=(const Status& rhs);

    // 移动构造(move)
    // 转移内部状态指针
    Status(Status&& rhs) noexcept;
    Status& operator=(Status&& rhs) noexcept;

    // 返回各个状态(succeed | error)
    static Status OK();
    static Status NotFound(const Slice& msg, const Slice& msg2 = Slice());
    // 数据损坏
    static Status Corruption(const Slice& msg, const Slice& msg2 = Slice());
    // 当前操作不支持
    static Status NotSupported(const Slice& msg, const Slice& msg2 = Slice());
    // 参数非法
    static Status InvalidArgument(const Slice& msg, const Slice& msg2 = Slice());
    static Status IOError(const Slice& msg, const Slice& msg2 = Slice());

    // 判断当前状态类型
    bool ok() const;
    bool IsNotFound() const;
    bool IsCorruption() const;
    bool IsNotSupportedError() const;
    bool IsInvalidArgument() const;
    bool IsIOError() const;

    std::string ToString() const;

private:
    // 所有支持的错误码枚举
    enum Code {
        kOk = 0,
        kNotFound = 1,
        kCorruption = 2,
        kNotSupported = 3,
        kInvalidArgument = 4,
        kIOError = 5
    };

    // 解析错误码
    Code code() const;

    // 构造一个失败状态
    Status(Code code, const Slice& msg, const Slice& msg2);

    // 神拷贝一份内部状态内存
    static const char* CopyState(const char* state);

    const char* state_;
};

}