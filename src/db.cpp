#include "db.h"

#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================
// 实现说明（对照 examples/mysql_demo.cpp 的 10 个步骤）
//
//   demo [1][2][3]  ->  connect()
//   demo [8]        ->  ensure_connected()   （多了"重连"）
//   demo [5][6]     ->  log_infer()
//   demo [7]        ->  recent_json()        （结果转成 JSON）
//   demo [10]       ->  ~Db()
//
// demo 里没有的两步：CREATE TABLE（表已存在）、DROP TABLE（不要删表）
//
// 注意：MySQL C API 是 C 风格 —— 不抛异常，靠返回值报错。
//   失败返回 NULL（mysql_init / mysql_real_connect）
//   失败返回非 0（mysql_query / mysql_set_character_set / mysql_ping）
//   错误信息用 mysql_error(c) 取
// 所以这里不用 try/catch。
// ============================================================

Db::Db() : conn_(nullptr) {}

Db::~Db()
{
    if (conn_ != nullptr) {
        mysql_close((MYSQL*)conn_);
        conn_ = nullptr;
    }
}

bool Db::connect(const std::string& host, const std::string& user,
                 const std::string& pass, const std::string& dbname)
{
    // 先记住参数：ensure_connected() 重连时要用
    host_   = host;
    user_   = user;
    pass_   = pass;
    dbname_ = dbname;

    // ① 创建连接对象（失败返回 NULL）
    MYSQL* c = mysql_init(nullptr);
    if (c == nullptr) {
        fprintf(stderr, "[db] mysql_init 失败\n");
        return false;
    }

    // ② 连接
    //    参数：连接对象, 主机, 用户, 密码, 库名, 端口(0=默认3306), socket(nullptr), flags(0)
    //    失败返回 NULL
    if (mysql_real_connect(c, host_.c_str(), user_.c_str(), pass_.c_str(),
                           dbname_.c_str(), 0, nullptr, 0) == nullptr) {
        fprintf(stderr, "[db] 连接失败: %s\n", mysql_error(c));
        mysql_close(c);
        return false;
    }

    // ③ 字符集：不设的话中文文件名入库会乱码
    if (mysql_set_character_set(c, "utf8mb4") != 0) {
        fprintf(stderr, "[db] 设置字符集失败: %s\n", mysql_error(c));
        // 不算致命错误，继续
    }

    conn_ = c;
    fprintf(stderr, "[db] 已连接 %s@%s/%s\n",
            user_.c_str(), host_.c_str(), dbname_.c_str());
    return true;
}

bool Db::ensure_connected()
{
    if (conn_ == nullptr) {
        return false;
    }

    // mysql_ping：0 = 连接还活着；非 0 = 断了
    //   MySQL 的 wait_timeout（默认 8 小时）会把空闲连接服务端单方面断开，
    //   而客户端在一段时间内还以为是连着的 —— 所以每次操作前都要检查。
    if (mysql_ping((MYSQL*)conn_) == 0) {
        return true;
    }

    fprintf(stderr, "[db] 连接已失效，正在重连...\n");

    // 重连 = 关掉旧的 + 重新 init + 重新 connect
    // （不要在已失效的 handle 上直接再 connect，内部状态是坏的，行为不可靠）
    mysql_close((MYSQL*)conn_);
    conn_ = nullptr;

    MYSQL* c = mysql_init(nullptr);
    if (c == nullptr) {
        return false;
    }

    if (mysql_real_connect(c, host_.c_str(), user_.c_str(), pass_.c_str(),
                           dbname_.c_str(), 0, nullptr, 0) == nullptr) {
        fprintf(stderr, "[db] 重连失败: %s\n", mysql_error(c));
        mysql_close(c);
        return false;
    }

    // 重连后字符集可能被重置，重新设一次
    mysql_set_character_set(c, "utf8mb4");

    conn_ = c;
    fprintf(stderr, "[db] 重连成功\n");
    return true;
}

bool Db::log_infer(const std::string& image_name, const std::string& image_hash,
                   int width, int height, int cost_ms)
{
    if (!ensure_connected()) {
        return false;
    }
    MYSQL* c = (MYSQL*)conn_;

    // ① result_json 用 nlohmann/json 生成（保证转义正确）
    nlohmann::json rj;
    rj["width"]  = width;
    rj["height"] = height;
    rj["format"] = "png";
    const std::string rjs = rj.dump();

    // ② 转义三个字符串 —— 防止 SQL 注入
    //    image_name 来自客户端，必须当"数据"而不是"语法"对待
    char esc_name[1024];
    char esc_hash[256];
    char esc_json[2048];

    mysql_real_escape_string(c, esc_name, image_name.c_str(),
                             (unsigned long)image_name.size());
    mysql_real_escape_string(c, esc_hash, image_hash.c_str(),
                             (unsigned long)image_hash.size());
    mysql_real_escape_string(c, esc_json, rjs.c_str(),
                             (unsigned long)rjs.size());

    // ③ 拼 INSERT（id 和 created_at 数据库自动填）
    char sql[4096];
    int n = snprintf(sql, sizeof(sql),
        "INSERT INTO infer_log (image_name, image_hash, result_json, cost_ms) "
        "VALUES ('%s', '%s', '%s', %d)",
        esc_name, esc_hash, esc_json, cost_ms);

    if (n < 0 || n >= (int)sizeof(sql)) {
        fprintf(stderr, "[db] INSERT 语句太长（%d 字节）\n", n);
        return false;
    }

    // ④ 执行（mysql_query 返回 0 才是成功）
    if (mysql_query(c, sql) != 0) {
        fprintf(stderr, "[db] INSERT 失败: %s\n", mysql_error(c));
        return false;
    }
    return true;
}

std::string Db::recent_json(int limit)
{
    if (!ensure_connected()) {
        return "[]";
    }
    MYSQL* c = (MYSQL*)conn_;

    if (limit <= 0)  limit = 10;
    if (limit > 200) limit = 200;       // 防止一次拉太多

    // ① 拼 SELECT
    //    ORDER BY id DESC = 最新的在前（走主键索引，只扫 limit 行）
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT id, image_name, image_hash, cost_ms, created_at "
        "FROM infer_log ORDER BY id DESC LIMIT %d", limit);

    // ② 执行
    if (mysql_query(c, sql) != 0) {
        fprintf(stderr, "[db] SELECT 失败: %s\n", mysql_error(c));
        return "[]";
    }

    MYSQL_RES* res = mysql_store_result(c);
    if (res == nullptr) {
        fprintf(stderr, "[db] store_result 失败: %s\n", mysql_error(c));
        return "[]";
    }

    // ③ 逐行转成 JSON
    //    注意：每列都是 char* 字符串，且可能是 NULL
    nlohmann::json arr = nlohmann::json::array();

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        nlohmann::json item;
        item["id"]         = row[0] ? atol(row[0]) : 0;
        item["image_name"] = row[1] ? row[1] : "";
        item["image_hash"] = row[2] ? row[2] : "";
        item["cost_ms"]    = row[3] ? atol(row[3]) : 0;
        item["created_at"] = row[4] ? row[4] : "";
        arr.push_back(item);
    }

    // ④ ★ 必须释放结果集，否则每次查询泄漏一块内存
    mysql_free_result(res);

    return arr.dump();
}
