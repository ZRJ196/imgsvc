// ============================================================
// MySQL C API 最小示例
//
// 目的：用一段可以跑通的线性代码，把 db.cpp 需要用的 API 全部过一遍。
//
// 编译：
//   g++ -std=c++17 -o /tmp/mysql_demo examples/mysql_demo.cpp -lmysqlclient
// 运行：
//   /tmp/mysql_demo
//
// 它只操作自己建的 demo 表，最后会删掉，不碰 infer_log。
// ============================================================

#include <mysql/mysql.h>

#include <cstdio>
#include <cstring>

int main()
{
    // ============ 1. mysql_init：创建连接对象 ============
    // 参数传 nullptr 即可；失败返回 NULL（内存不足时）
    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr) {
        fprintf(stderr, "mysql_init 失败\n");
        return 1;
    }
    printf("[1] mysql_init 成功，连接对象 = %p\n", (void*)conn);

    // ============ 2. mysql_real_connect：连接 ============
    //   参数依次：连接对象, 主机, 用户, 密码, 数据库,
    //             端口(0 = 默认 3306), unix_socket(nullptr), flags(0)
    //   失败返回 NULL
    if (mysql_real_connect(conn, "127.0.0.1", "imgsvc", "imgsvc123", "imgsvc",
                           0, nullptr, 0) == nullptr) {
        fprintf(stderr, "[2] 连接失败: %s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    printf("[2] 连接成功\n");

    // ============ 3. mysql_set_character_set：字符集 ============
    //   不设的话，中文可能变成乱码（连接字符集和表的字符集对不上）
    if (mysql_set_character_set(conn, "utf8mb4") != 0) {
        fprintf(stderr, "[3] 设置字符集失败: %s\n", mysql_error(conn));
    }
    printf("[3] 字符集 = %s\n", mysql_character_set_name(conn));

    // ============ 4. mysql_query + CREATE ============
    //   mysql_query 返回 0 = 成功，非 0 = 失败
    mysql_query(conn, "DROP TABLE IF EXISTS demo");
    if (mysql_query(conn,
            "CREATE TABLE demo ("
            "  id INT AUTO_INCREMENT PRIMARY KEY,"
            "  name VARCHAR(100) NOT NULL,"
            "  score INT NOT NULL)") != 0) {
        fprintf(stderr, "[4] 建表失败: %s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    printf("[4] 建表成功\n");

    // ============ 5. mysql_real_escape_string：转义（防注入）============
    //   这是"把用户输入当数据、而不是当语法"的关键一步
    const char* evil = "攻击者'); DROP TABLE demo; -- ";
    char safe[512];
    mysql_real_escape_string(conn, safe, evil, (unsigned long)strlen(evil));
    printf("[5] 转义演示\n");
    printf("    原始输入: %s\n", evil);
    printf("    转义之后: %s\n", safe);
    printf("    ↑ 引号前被加了反斜杠，所以它跑不出字符串边界\n");

    // ============ 6. INSERT ============
    char sql[1024];
    int n = snprintf(sql, sizeof(sql),
        "INSERT INTO demo (name, score) VALUES ('%s', %d)", safe, 88);
    if (n < 0 || n >= (int)sizeof(sql)) {
        fprintf(stderr, "[6] SQL 太长\n");
        mysql_close(conn);
        return 1;
    }
    if (mysql_query(conn, sql) != 0) {
        fprintf(stderr, "[6] INSERT 失败: %s\n", mysql_error(conn));
    } else {
        printf("[6] INSERT 成功，影响 %llu 行\n",
               (unsigned long long)mysql_affected_rows(conn));
    }

    // 再插两条正常数据（一次插多条）
    mysql_query(conn, "INSERT INTO demo (name, score) VALUES ('张三', 90), ('李四', 75)");

    // ============ 7. SELECT + 遍历结果集 ============
    if (mysql_query(conn, "SELECT id, name, score FROM demo ORDER BY score DESC") != 0) {
        fprintf(stderr, "[7] SELECT 失败: %s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }

    // mysql_store_result：把整个结果集取回客户端内存
    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) {
        fprintf(stderr, "[7] store_result 失败: %s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }

    printf("[7] 查询结果（%llu 行）：\n", (unsigned long long)mysql_num_rows(res));
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        // ★ 每一列都是 char* 字符串，而且可能是 NULL —— 必须判空
        const char* id    = row[0] ? row[0] : "NULL";
        const char* name  = row[1] ? row[1] : "NULL";
        const char* score = row[2] ? row[2] : "NULL";
        printf("    id=%s  name=%s  score=%s\n", id, name, score);
    }

    // ★★ 必须释放，否则每次查询泄漏一块内存
    mysql_free_result(res);
    printf("[7] mysql_free_result 已调用\n");

    // ============ 8. mysql_ping：检查连接是否还活着 ============
    //   返回 0 = 正常；非 0 = 断了（比如超过 wait_timeout 被服务端断开）
    printf("[8] mysql_ping = %d（0 表示连接正常）\n", mysql_ping(conn));

    // ============ 9. DROP 演示表 ============
    mysql_query(conn, "DROP TABLE IF EXISTS demo");
    printf("[9] demo 表已删除\n");

    // ============ 10. mysql_close：关闭连接 ============
    mysql_close(conn);
    printf("[10] 连接已关闭\n");

    // ============ 附：连接失败长什么样 ============
    printf("\n---- 附：用错误密码连接，看错误信息 ----\n");
    MYSQL* bad = mysql_init(nullptr);
    if (mysql_real_connect(bad, "127.0.0.1", "imgsvc", "wrongpass", "imgsvc",
                           0, nullptr, 0) == nullptr) {
        printf("    连接失败: %s\n", mysql_error(bad));
        printf("    错误码  : %u\n", mysql_errno(bad));
    } else {
        printf("    （不该成功）\n");
    }
    mysql_close(bad);

    return 0;
}
