#pragma once

#include <string>

// 推理记录的数据库封装
//
// 为什么是"类"而不是"函数"？
//   数据库连接要一直保持（长连接复用），所以连接对象必须比单次调用活得更久。
//   和 InferEngine 是同一个道理。
class Db {
public:
    Db();
    ~Db();

    // 禁止拷贝（连接对象不能被复制）
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    // 建立连接
    bool connect(const std::string& host, const std::string& user,
                 const std::string& pass, const std::string& dbname);

    bool ok() const { return conn_ != nullptr; }

    // 插入一条推理记录
    //   image_name —— 文件名（客户端提供，入库前必须转义）
    //   image_hash —— 图片内容 SHA-256
    //   width/height —— 原图尺寸
    //   cost_ms —— 推理耗时（毫秒）
    bool log_infer(const std::string& image_name, const std::string& image_hash,
                   int width, int height, int cost_ms);

    // 取最近 limit 条记录，返回 JSON 数组字符串（失败返回 "[]"）
    std::string recent_json(int limit);

private:
    // 检查连接是否还活着（断了就重连）
    bool ensure_connected();

    void* conn_;        // 实际是 MYSQL*，用 void* 避免头文件里引入 mysql.h
    std::string host_, user_, pass_, dbname_;
};
