// 声明多路归并 iterator
// 用于合并 MemTable，immutable MemTable，多个 SSTable iterator
#pragma once

#include <vector>

#include <lindb/iterator.h>

namespace lindb {

class Comparator;

Iterator* NewMergingIterator(const Comparator* comparator, std::vector<Iterator*> children);

}