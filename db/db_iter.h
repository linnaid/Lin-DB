// 把 internal iterator 转换成用户可见 iterator
// 它是用来过滤一些内部数据，为了让用户看到的key是唯一的，分为正向遍历和反向遍历
#pragma once

#include <lindb/iterator.h>

#include "Lin-DB/db/dbformat.h"

namespace lindb {

Iterator* NewDBIterator(const Comparator* user_comparator, Iterator* internal_iter, SequenceNumber sequence);

}