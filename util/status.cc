#include <lindb/status.h>
#include <cstdint>
#include <cstring>
#include <assert.h>


namespace lindb {

Status::Status() noexcept : state_(nullptr) {}

Status::~Status() {
    delete[] state_;
}

Status::Status(const Status& rhs)
    : state_(rhs.state_ == nullptr ? nullptr : CopyState(rhs.state_)) {}

Status& Status::operator=(const Status& rhs) {
    if (this != &rhs) {
        delete[] state_;
        state_ = (rhs.state_ == nullptr) ? nullptr : CopyState(rhs.state_);
    }
    return *this;
} 

Status::Status(Status&& rhs) noexcept
    : state_(rhs.state_) {
        rhs.state_ = nullptr;
    }

Status& Status::operator=(Status&& rhs) noexcept {
    if (this != &rhs) {
        delete[] state_;
        state_ = rhs.state_;
        rhs.state_ = nullptr;
    }
    return *this;
}

Status Status::OK() {
    return Status();
}

Status Status::NotFound(const Slice& msg, const Slice& msg2) {
    return Status(kNotFound, msg, msg2);
}

Status Status::Corruption(const Slice& msg, const Slice& msg2) {
    return Status(kCorruption, msg, msg2);
}

Status Status::NotSupported(const Slice& msg, const Slice& msg2) {
    return Status(kNotSupported, msg, msg2);
}

Status Status::InvalidArgument(const Slice& msg, const Slice& msg2) {
    return Status(kInvalidArgument, msg, msg2);
}

Status Status::IOError(const Slice& msg, const Slice& msg2) {
    return Status(kIOError, msg, msg2);
}

bool Status::ok() const {
    return state_ == nullptr;
}

bool Status::IsNotFound() const {
    return code() == kNotFound;
}

bool Status::IsCorruption() const {
    return code() == kCorruption;
}

bool Status::IsNotSupportedError() const {
    return code() == kNotSupported;
}

bool Status::IsInvalidArgument() const {
    return code() == kInvalidArgument;
}

bool Status::IsIOError() const {
    return code() == kIOError;
}

Status::Code Status::code() const {
    if (state_ == nullptr) {
        return kOk;
    }
    return static_cast<Code>(static_cast<unsigned char>(state_[4]));
}
// 内部状态内存分配：4字节信息长度 + 1字节错误码 + size 字节消息体
const char* Status::CopyState(const char* state) {
    uint32_t size = 0;
    std::memcpy(&size, state, sizeof(size));

    char* result = new char[size + 5];
    std::memcpy(result, state, size + 5);

    return result;
}

Status::Status(Code code, const Slice& msg, const Slice& msg2) 
    : state_(nullptr) {
        assert(code != kOk);

        const uint32_t len1 = static_cast<uint32_t>(msg.size());
        const uint32_t len2 = static_cast<uint32_t>(msg2.size());
        const uint32_t size = len1 + (len2 == 0 ? 0 : (2 + len2));

        char* result = new char[size + 5];
        std::memcpy(result, &size, sizeof(size));
        result[4] = static_cast<char>(code);
        std::memcpy(result+5, msg.data(), len1);

        if (len2 != 0) {
            result[5 + len1] = ':';
            result[6 + len1] = ' ';
            std::memcpy(result + 7 + len1, msg2.data(), len2);
        }
        state_ = result;
    }

std::string Status::ToString() const {
    if (state_ == nullptr) {
        return "OK";
    }

    const char* type = nullptr;
    char tmp[32];

    switch (code()) {
    case kOk:
        type = "OK";
        break;
    case kNotFound:
        type = "NotFound: ";
        break;
    case kCorruption:
        type = "Corruption: ";
        break;
    case kNotSupported:
        type = "Not implemented: ";
        break;
    case kInvalidArgument:
        type = "Invalid argument: ";
        break;
    case kIOError:
        type = "IO error: ";
        break;
    default:
        std::snprintf(tmp, sizeof(tmp), "Unknown code(%d): ", static_cast<int>(code()));
        type = tmp;
    }
    uint32_t length = 0;
    std::memcpy(&length, state_, sizeof(length));
    
    std::string result(type);
    result.append(state_ + 5, length);
    return result;
}


}