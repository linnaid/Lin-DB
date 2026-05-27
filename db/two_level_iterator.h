// 声明 NewTwoLevelIterator 工厂和 block 加载函数类型
#pragma once

#include <lindb/iterator.h>

namespace lindb {

struct ReadOptions;

using BlockFunction = Iterator* (*)(void* arg, const ReadOptions& options, const Slice& index_value);

Iterator* NewTwoLevelIterator(Iterator* index_iter, BlockFunction block_function, void* arg, const ReadOptions& options);

}