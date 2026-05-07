#include <memory>  // 使用 std::unique_ptr 自动释放 DB 对象。
#include <string>  // 使用 std::string 保存 Get 输出的 value。

#include <lindb/db.h>  // 使用 DB 公共接口测试 Open/Put/Delete/Get。
#include <lindb/options.h>  // 使用 Options、ReadOptions、WriteOptions。

#include "tests/test_util.h"  // 使用项目已有的轻量测试断言宏。

int main() {  // 测试 DB 最小内核的入口函数。
    lindb::Options options;  // 创建默认 DB 配置，目前主要提供默认 comparator。
    lindb::DB* raw_db = nullptr;  // 准备接收 DB::Open 创建出来的 DB 指针。
    lindb::Status open_status = lindb::DB::Open(options, "db-test-memory-only", &raw_db);  // 打开一个当前只存在内存中的 DB。
    LINDB_EXPECT_TRUE(open_status.ok());  // 验证 Open 成功。
    LINDB_EXPECT_TRUE(raw_db != nullptr);  // 验证 Open 返回了有效 DB 对象。

    std::unique_ptr<lindb::DB> db(raw_db);  // 用 unique_ptr 接管 DB 生命周期，避免测试结束时泄漏。
    lindb::ReadOptions read_options;  // 创建默认读选项，目前为空结构，给未来扩展预留接口。
    lindb::WriteOptions write_options;  // 创建默认写选项，目前为空结构，给未来扩展预留接口。
    std::string value;  // 保存 DB::Get 返回的 value。

    lindb::Status missing_status = db->Get(read_options, "missing", &value);  // 查询一个从未写入过的 key。
    LINDB_EXPECT_TRUE(missing_status.IsNotFound());  // 验证不存在的 key 返回 NotFound。

    LINDB_EXPECT_TRUE(db->Put(write_options, "key", "v1").ok());  // 写入 key 的第一个版本 v1。
    LINDB_EXPECT_TRUE(db->Get(read_options, "key", &value).ok());  // 读取 key 的当前可见版本。
    LINDB_EXPECT_EQ(value, std::string("v1"));  // 验证当前可见 value 是 v1。

    LINDB_EXPECT_TRUE(db->Put(write_options, "key", "v2").ok());  // 再次写入同一个 key，形成更新版本 v2。
    LINDB_EXPECT_TRUE(db->Get(read_options, "key", &value).ok());  // 再次读取 key 的当前可见版本。
    LINDB_EXPECT_EQ(value, std::string("v2"));  // 验证新版本 v2 覆盖旧版本 v1。

    LINDB_EXPECT_TRUE(db->Delete(write_options, "key").ok());  // 给 key 写入一条删除标记。
    lindb::Status deleted_status = db->Get(read_options, "key", &value);  // 删除后再次读取 key。
    LINDB_EXPECT_TRUE(deleted_status.IsNotFound());  // 验证删除标记会让 key 对外表现为不存在。

    LINDB_EXPECT_TRUE(db->Put(write_options, "key", "v3").ok());  // 删除后重新写入 key 的新版本 v3。
    LINDB_EXPECT_TRUE(db->Get(read_options, "key", &value).ok());  // 读取重新写入后的 key。
    LINDB_EXPECT_EQ(value, std::string("v3"));  // 验证删除后的新 Put 可以重新让 key 可见。

    LINDB_EXPECT_TRUE(db->Put(write_options, "empty-value", "").ok());  // 写入一个空 value，验证 Slice 能表示空字节串。
    LINDB_EXPECT_TRUE(db->Get(read_options, "empty-value", &value).ok());  // 读取空 value key。
    LINDB_EXPECT_EQ(value, std::string(""));  // 验证空 value 不会被误判为 NotFound。

    return 0;  // 所有检查通过，测试进程正常退出。
}  // 结束 main。
