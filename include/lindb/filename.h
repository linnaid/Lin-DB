#pragma once

#include <cstdint>
#include <string>

#include <lindb/status.h>

namespace lindb {
class Env;

enum FileType {
    kLogFile,
    kDBLockFile,
    kTableFile,
    kDescriptorFile,
    kTempFile,
    kInfoLogFile,
    kCurrentFile
};

// WAL 文件名
std::string LogFileName(const std::string& dbname, uint64_t number);
std::string TableFileName(const std::string& dbname, uint64_t number);
// MANIFEST 文件名生成
std::string DescriptorFileName(const std::string& dbnam, uint64_t number);
std::string CurrentFileName(const std::string& dbname);
std::string LockFileName(const std::string& dbname);
std::string TempFileName(const std::string& dbname, uint64_t number);
std::string InfoLogFileName(const std::string& dbname);
std::string OldInfoLogFileName(const std::string& dbname);
// 解析文件名
bool ParseFileName(const std::string& filename, uint64_t* number, FileType* type);
// 更新 CURRENT
Status SetCurrentFile(Env* env, const std::string& dbname, uint64_t descriptor_number);
}