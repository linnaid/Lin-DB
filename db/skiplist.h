// 负责有序索引
#pragma once

#include <atomic>
#include <cassert>

#include "Lin-DB/util/arena.h"
#include "Lin-DB/util/random.h"

namespace lindb {

// 跳表：支持有序插入、查找、遍历的多层链表
// 特点：
// 1. 节点插入后不删除
// 2. 写入需要外部同步
// 3. 读取不加锁，但要求 SkipList 生命周期覆盖读取过程
template <typename Key, class Comparator>
class SkipList {
private:
    // 保存一个 key，以及若干层前向指针 next_[i]
    // 节点高度不固定，所以 next_[1] 是"柔性数组技巧"
    // 真正内存大小由 NewNode(key, height) 动态申请
    struct Node;
public:
    explicit SkipList(Comparator cmp, Arena* arena);

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    // 插入一个 key
    // 要求：当前表中不存在"比较结果相等"的 key
    void Insert(const Key& key);

    // 判断是否包含某个 key
    bool Contains(const Key& key) const;

    // 迭代器，负责顺序遍历 SkipList
    class Iterator {
    public:
        // 绑定到某个 SkipList，初始时无效
        explicit Iterator(const SkipList* list);

        // 当前是否指向有效节点
        bool Valid() const;

        // 返回当前节点的 key
        // 要求：Valid() == true
        const Key& key() const;

        // 前进一步，到下一节点
        void Next();

        // 后退一步
        // 跳表本身没有 prev 指针，所以这里要重新搜索"前驱"
        void Prev();

        // 定位到第一个 >= target 的节点
        void Seek(const Key& target);

        // 定位到最小节点
        void SeekToFirst();

        // 定位到最大节点
        void SeekToLast();
    
    private:
        // 当前迭代的跳表
        const SkipList* list_;

        // 当前所指向的节点；nullptr 表示无效
        Node* node_;
    };

private:
    // 跳表允许的最大高度
    enum { kMaxHeight = 12 };

    // 返回当前跳表实际使用的最高层数
    int GetMaxHeight() const {
        return max_height_.load(std::memory_order_relaxed);
    }

    // 创建一个指定高度的节点
    Node* NewNode(const Key& key, int height);

    // 随机生成节点高度
    // 高度越高节点越少，用于保持跳表期望复杂度
    int RandomHeight();

    // 判断两个 key 是否像等
    bool Equal(const Key& a, const Key& b) const {
        return compare_(a, b) == 0;
    }

    // 判断给定 key 是否严格大于节点 n 的 key
    // 如果 n == nullptr，视为正无穷，不再向右走
    bool KeyIsAfterNode(const Key& key, Node* n) const;

    // 找到第一个 >= key 的节点
    // 如果 prev != nullptr，则顺便把每一层的前驱节点填进 prev[]
    Node* FindGreaterOrEqual(const Key& key, Node** prev) const;

    // 找到最后一个 < key 的节点
    // 不存在返回 head_
    Node* FindLessThan(const Key& key) const;

    // 找到整个表的最后一个节点，表空返回 head_
    Node* FindLast() const;

    // 比较器：定义 SkipList 的排序规则
    const Comparator compare_;

    Arena* const arena_;

    // 哨兵节点/头节点
    Node* const head_;

    // 当前跳表实际高度
    std::atomic<int> max_height_;

    // 随机数发生器，用于决定新节点高度
    Random rnd_;
};

template<typename Key, class Comparator>
struct SkipList<Key, Comparator>::Node{
    explicit Node(const Key& k) : key(k) {}

    // 节点保存的键，插入后不再修改
    const Key key;

    // 带  acquire 语义读取第 n 层 next 指针
    Node* Next(int n) {
        assert(n >= 0);
        // load 是原子读指针；store 是原子写指针
        return next_[n].load(std::memory_order_acquire);
    }

    // 带 release 语义发布第 n 层 next 指针；
    void SetNext(int n, Node* x) {
        assert(n >= 0);
        next_[n].store(x, std::memory_order_release);
    }

    // relaxed 读取 next 指针
    Node* NoBarrier_Next(int n) {
        assert(n >= 0);
        return next_[n].load(std::memory_order_relaxed);
    }

    // relaxed 写入 next 指针
    void NoBarrier_SetNext(int n, Node* x) {
        assert(n >= 0);
        next_[n].store(x, std::memory_order_relaxed);
    }

private:
    // 第 0 层是最底层链表
    std::atomic<Node*> next_[1];
};

}

#define LINDB_SKIPLIST_IMPLEMENTATION_INCLUDE_FROM_HEADER
#include "skiplist.cc"
#undef LINDB_SKIPLIST_IMPLEMENTATION_INCLUDE_FROM_HEADER