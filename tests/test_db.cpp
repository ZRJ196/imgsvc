// db 模块的独立测试程序
// 用法：./build/test_db
//
// 它只测数据库模块，不涉及模型和网络——这样出问题时能立刻定位是哪一层。

#include "db.h"

#include <cstdio>
#include <string>

int main()
{
    printf("=== 1. 连接数据库 ===\n");
    Db db;
    if (!db.connect("127.0.0.1", "imgsvc", "imgsvc123", "imgsvc")) {
        printf("  ✗ 连接失败\n");
        return 1;
    }
    printf("  ✓ 连接成功\n\n");

    printf("=== 2. 插入一条记录 ===\n");
    std::string fake_hash =
        "aaaa0000aaaa0000aaaa0000aaaa0000aaaa0000aaaa0000aaaa0000aaaa0000";
    if (!db.log_infer("测试图片.png", fake_hash, 800, 600, 123)) {
        printf("  ✗ 插入失败\n");
        return 1;
    }
    printf("  ✓ 插入成功\n\n");

    printf("=== 3. 查询最近 5 条 ===\n");
    std::string js = db.recent_json(5);
    printf("  返回 %zu 字节：\n  %s\n\n", js.size(), js.c_str());

    printf("=== 4. 检查结果 ===\n");
    if (js == "[]") {
        printf("  ✗ 查询返回空数组，检查 recent_json 的实现\n");
        return 1;
    }
    if (js.find("测试图片.png") == std::string::npos) {
        printf("  ✗ 返回的 JSON 里没有 image_name\n");
        return 1;
    }
    if (js.find("\"cost_ms\":123") == std::string::npos) {
        printf("  ✗ 返回的 JSON 里没有 cost_ms（数字类型可能拼错了）\n");
        return 1;
    }
    printf("  ✓ JSON 内容和预期一致\n\n");

    printf("=== 全部通过 ===\n");
    printf("\n清理测试数据：\n");
    printf("  mysql -uimgsvc -pimgsvc123 imgsvc -e \"DELETE FROM infer_log WHERE image_name='测试图片.png';\"\n");
    return 0;
}
