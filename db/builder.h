// 声明 MemTable iterator 刷盘成 SSTable 的 helper 接口
#pragma once 

#include <string>

#include <lindb/options.h>
#include <lindb/status.h>

namespace lindb {

class Env;
class Iterator;
class FileMetaData;

// 把 iter 中的有序 internal key/value 写成 meta->number 对应的 .ldb 
// 并回填 file_size, smallest, largest
Status BuildTable(const std::string& dbname, Env* env, const Options& options, Iterator* iter, FileMetaData* meta);

}