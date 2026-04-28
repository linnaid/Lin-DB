// 定义用户可见的批量写接口，只暴露逻辑操作，不暴露 sequence/count 等内部字段
#pragma once

#include <string>
#include <lindb/slice.h>
#include <lindb/status.h>

namespace lindb{

// 定义一批写操作的拥有型容器
class WriteBatch {
public:
    WriteBatch();
    // 允许默认拷贝构造和赋值，方便用户在函数间传递 WriteBatch 对象
    WriteBatch(const WriteBatch&) = default;
    WriteBatch& operator=(const WriteBatch&) = default;

    ~WriteBatch() = default;

    // 定义回放 batch 时的回调接口
    class Handler {
    public:
        virtual ~Handler() = default;

        // 回放一条 put 记录
        virtual void Put(const Slice& key, const Slice& value) = 0;
        virtual void Delete(const Slice& key) = 0;
    };

    // 添加一条 put 记录
    void Put(const Slice& key, const Slice& value);
    void Delete(const Slice& key);
    // 清空 batch，并重新初始化空 header
    void Clear();
    // 把另一个 batch 的 records 追加到当前 batch
    void Append(const WriteBatch& source);
    // 顺序解码 records，并回调给 handler
    Status Iterate(Handler* handler) const;

private:
    // 保存完整 batch 编码：header + records
    std::string rep_;

    // 允许内部工具类访问 rep_、sequence、count
    friend class WriteBatchInternal;
};

}

