#include <lindb/cache.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

#include "Lin-DB/util/hash.h"
#include "Lin-DB/util/coding.h"

namespace lindb {

struct LRUEntry : public Cache::Handle {
    void* value;
    void (*deleter)(const Slice&, void*);
    LRUEntry* next_hash;
    LRUEntry* prev;
    LRUEntry* next;
    // 该条目占用的容量
    size_t charge;
    size_t key_length;
    bool in_cache;
    uint32_t refs;
    uint32_t hash;
    char key_data[1];

    Slice key() const { return Slice(key_data, key_length); }
};

class HandleTable {
public:
    HandleTable() : list_(nullptr), length_(0), count_(0) { Resize(); }
    ~HandleTable() { free(list_); }
    
    LRUEntry** FindPointer(const Slice& key, uint32_t hash) {
        LRUEntry** ptr = &list_[hash & (length_ - 1)];
        while (*ptr != nullptr && ((*ptr)->hash != hash || key != (*ptr)->key())) {
            ptr = &(*ptr)->next_hash;
        }
        return ptr;
    }

    LRUEntry* Lookup(const Slice& key, uint32_t hash) {
        return *FindPointer(key, hash);
    }

    LRUEntry* Insert(LRUEntry* entry) {
        LRUEntry** ptr = FindPointer(entry->key(), entry->hash);
        LRUEntry* old = *ptr;
        entry->next_hash = (old == nullptr ? nullptr : old->next_hash);
        *ptr = entry;
        if (old == nullptr) {
            ++count_;
            if (count_ > length_) {
                Resize();
            }
        }
        return old;
    }

    LRUEntry* Remove(const Slice& key, uint32_t hash) {
        LRUEntry** ptr = FindPointer(key, hash);
        LRUEntry* result = *ptr;
        if (result != nullptr) {
            *ptr = result->next_hash;
            --count_;
        }
        return result;
    }

private:
    void Resize() {
        uint32_t new_length = 4;
        while (new_length < count_) {
            new_length *= 2;
        }
        LRUEntry** new_list = static_cast<LRUEntry**>(calloc(new_length, sizeof(*new_list)));
        uint32_t old_count = 0;
        for (uint32_t i = 0; i < length_; i++) {
            LRUEntry* entry = list_[i];
            while (entry != nullptr)
            {
                LRUEntry* next = entry->next_hash;
                uint32_t bucket = entry->hash & (new_length - 1);
                entry->next_hash = new_list[bucket];
                new_list[bucket] = entry;
                entry = next;
                ++old_count;
            }
        }
        assert(old_count == count_);
        free(list_);
        list_ = new_list;
        length_= new_length;
    }

    // hash 桶数组，长 length_，每个桶是一个链表头指针
    LRUEntry** list_;
    uint32_t length_;
    uint32_t count_;
};

// LRU 缓存实现
class LRUCacheImpl : public Cache {
public:
    explicit LRUCacheImpl(size_t capacity)
    : capacity_(capacity), usage_(0) {
        lru_.next = &lru_;
        lru_.prev = &lru_;
        in_use_.next = &in_use_;
        in_use_.prev = &in_use_;
    }

    ~LRUCacheImpl() override {
        assert(in_use_.next == &in_use_);
        for (LRUEntry* e = lru_.next; e != &lru_; ) {
            LRUEntry* next = e->next;
            table_.Remove(e->key(), e->hash);
            FreeEntry(e);
            e = next;
        }
    }

    Handle* Insert(const Slice& key, void* value, size_t charge, 
                void (*deleter)(const Slice& key, void* value)) override {
        if (capacity_ > 0 && charge > 0) {
            const uint32_t hash = Hash(key.data(), key.size(), 0);
            // 加锁区域(单线程下无需，保留结构为将来扩展)
            {
                LRUEntry* e = static_cast<LRUEntry*>(malloc(sizeof(LRUEntry) - 1 + key.size()));
                e->value = value;
                e->deleter = deleter;
                e->charge = charge;
                e->key_length = key.size();
                e->hash = hash;
                e->in_cache = true;
                e->refs = 1;
                std::memcpy(e->key_data, key.data(), key.size());

                LRUEntry* old = table_.Insert(e);
                e->in_cache = true;
                usage_ += charge;

                if (old != nullptr) {
                    old->in_cache = false;
                    if (old->refs == 1) {
                        LRU_Remove(old);
                        FreeEntry(old);
                    } else {
                        // 还有外部引用，推迟到最后一 Release 释放
                        // 已从哈希表移除，从 lru_ 移到 in_use_(其实已在 in_use_)
                        // 这里不释放，等外部 Release
                    }
                }

                LRU_Append(&in_use_, e);

                while (usage_ > 0 && usage_ > capacity_ && lru_.next != &lru_) {
                    LRUEntry* old_entry =  lru_.next;
                    assert(old_entry->refs == 1);
                    bool erased = (table_.Remove(old_entry->key(), old_entry->hash) != nullptr);
                    if (erased) {
                        old_entry->in_cache = false;
                        LRU_Remove(old_entry);
                        FreeEntry(old_entry);
                    }
                }

                return reinterpret_cast<Handle*>(e);
            }
        }
        return nullptr;
    }

    Handle* Lookup(const Slice& key) override {
        const uint32_t hash = Hash(key.data(), key.size(), 0);
        {
            LRUEntry* e = table_.Lookup(key, hash);
            if (e != nullptr) {
                Ref(e);
                return reinterpret_cast<Handle*>(e);
            }
        }
        return nullptr;
    }

    void Release(Handle* handle) override {
        LRUEntry* e = reinterpret_cast<LRUEntry*>(handle);
        Unref(e);
    }

    void* Value(Handle* handle) override {
        LRUEntry* e = reinterpret_cast<LRUEntry*>(handle);
        return e->value;
    }

    void Erase(const Slice& key) override {
        const uint32_t hash = Hash(key.data(), key.size(), 0);
        {
            LRUEntry* e = table_.Remove(key, hash);
            if (e != nullptr) {
                e->in_cache = false;
                if (e->refs == 1) {
                    LRU_Remove(e);
                    FreeEntry(e);
                } else {
                    // 有外部引用，延迟释放(最后一 Release 时释放)
                    // 但需要从 lru_ 移到适当位置
                }
            }
        }
    }

    uint64_t NewId() override {
        static uint64_t next_id = 0;
        return next_id++;
    }

private:
    // 增加引用计数(refs)
    void Ref(LRUEntry* e) {
        if (e->refs == 1 && e->in_cache) {
            LRU_Remove(e);
            LRU_Append(&in_use_, e);
        }
        ++e->refs;
    }

    // 减少引用计数(refs)
    void Unref(LRUEntry* e) {
        assert(e->refs > 0);
        --e->refs;
        if (e->refs == 0) {
            FreeEntry(e);
        } else if (e->refs == 1 && e->in_cache) {
            LRU_Remove(e);
            LRU_Append(&lru_, e);
        }
    }

    void LRU_Remove(LRUEntry* e) {
        e->next->prev = e->prev;
        e->prev->next = e->next;
    }

    void LRU_Append(LRUEntry* list, LRUEntry* e) {
        e->next = list;
        e->prev = list->prev;
        e->prev->next = e;
        e->next->prev = e;
    }

    void FreeEntry(LRUEntry* e) {
        assert(e->refs == 0);
        if (e->deleter) {
            (*e->deleter)(e->key(), e->value);
        }
        usage_ -= e->charge;
        free(e);
    }

    // 缓存容量上限
    size_t capacity_;
    // 当前使用量
    size_t usage_;

    // lru_ 是可淘汰条目(refs == 1)，按 LRU 顺序排列
    // in_use_ 是被外部引用的条目(refs >= 2)，不可淘汰
    LRUEntry lru_;
    LRUEntry in_use_;

    HandleTable table_;
};

Cache::~Cache() = default;

Cache* NewLRUCache(size_t capacity) {
    return new LRUCacheImpl(capacity);
}

}