#include "preprocess.h"
#include "infer.h"
#include "postprocess.h"
#include "hash.h"
#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>        // strncasecmp
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <chrono>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define STATIC_ROOT "static"
#define MODEL_PATH "models/u2net.onnx"

// TODO: 以后移到 config.json
#define DB_HOST "127.0.0.1"
#define DB_USER "imgsvc"
#define DB_PASS "imgsvc123"
#define DB_NAME "imgsvc"

#define MAX_REQUEST (10 * 1024 * 1024)
#define RECV_TIMEOUT_SEC 5
#define HISTORY_LIMIT 20

// ---------- 发数据 ----------

static void send_all(int fd, const void* data, size_t len)
{
    const char* p = (const char*)data;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            fprintf(stderr, "send 失败: %s\n", strerror(errno));
            return;
        }
        sent += (size_t)n;
    }
}

static void send_response(int conn_fd, const char* status, const char* content_type,
                          const void* body, size_t body_len)
{
    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);

    if (n < 0 || n >= (int)sizeof(header)) {
        fprintf(stderr, "响应头太长，被截断了 (n=%d)\n", n);
        return;
    }

    send_all(conn_fd, header, (size_t)n);
    if (body != NULL && body_len > 0) {
        send_all(conn_fd, body, body_len);
    }
}

// ---------- 读文件 ----------

static bool read_file(const char* filename, char** out, long* out_len)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return false;
    }

    long size = st.st_size;
    char* data = (char*)malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL) {
        close(fd);
        return false;
    }

    long got = 0;
    while (got < size) {
        ssize_t r = read(fd, data + got, (size_t)(size - got));
        if (r < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "read 失败: %s\n", strerror(errno));
            close(fd);
            free(data);
            return false;
        }
        if (r == 0) break;
        got += r;
    }

    close(fd);
    *out = data;
    *out_len = got;
    return true;
}

// ---------- 路径安全 ----------

static bool safe_path(const char* path, char* out, size_t out_size)
{
    static char static_root[PATH_MAX];
    static int  inited = 0;
    if (!inited) {
        if (realpath(STATIC_ROOT, static_root) == NULL) {
            fprintf(stderr, "找不到静态目录 %s: %s\n", STATIC_ROOT, strerror(errno));
            return false;
        }
        inited = 1;
    }

    char joined[1024];
    if (strcmp(path, "/") == 0) {
        snprintf(joined, sizeof(joined), "%s/index.html", STATIC_ROOT);
    } else {
        snprintf(joined, sizeof(joined), "%s%s", STATIC_ROOT, path);
    }

    char resolved[PATH_MAX];
    if (realpath(joined, resolved) == NULL) {
        return false;
    }

    size_t root_len = strlen(static_root);
    if (strncmp(resolved, static_root, root_len) != 0) {
        return false;
    }
    // 前缀校验的陷阱：后面必须紧跟 '/' 或结束
    if (resolved[root_len] != '\0' && resolved[root_len] != '/') {
        return false;
    }

    snprintf(out, out_size, "%s", resolved);
    return true;
}

static const char* content_type_of(const char* filename)
{
    const char* dot = strrchr(filename, '.');
    if (dot == NULL) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css")  == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js")   == 0) return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(dot, ".txt")  == 0) return "text/plain; charset=utf-8";
    if (strcmp(dot, ".png")  == 0) return "image/png";
    if (strcmp(dot, ".jpg")  == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(dot, ".ico")  == 0) return "image/x-icon";
    return "application/octet-stream";
}

// ---------- HTTP 头解析 ----------

static char* find_ci(char* haystack, const char* needle)
{
    size_t nlen = strlen(needle);
    for (char* p = haystack; *p != '\0'; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) {
            return p;
        }
    }
    return NULL;
}

static char* find_body_start(char* req)
{
    char* sep = strstr(req, "\r\n\r\n");
    return (sep == NULL) ? NULL : sep + 4;
}

// 从某个请求头里取出值（到行尾为止），去掉前导空格
static void header_value(char* req, const char* name, char* out, size_t out_size)
{
    out[0] = '\0';
    char* p = find_ci(req, name);
    if (p == NULL) return;

    p += strlen(name);
    while (*p == ' ' || *p == '\t') p++;

    size_t i = 0;
    while (*p != '\0' && *p != '\r' && *p != '\n' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

// ---------- main ----------

int main()
{
    // ===== 模型只加载一次，常驻内存 =====
    InferEngine engine(MODEL_PATH);
    if (!engine.ok()) {
        fprintf(stderr, "模型加载失败，服务端退出\n");
        return 1;
    }

    // ===== 数据库：连不上不致命，服务照常跑，只是不记录 =====
    Db db;
    if (!db.connect(DB_HOST, DB_USER, DB_PASS, DB_NAME)) {
        fprintf(stderr, "数据库连接失败，推理记录将不会入库\n");
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket创建失败");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind 绑定失败");
        return 1;
    }
    if (listen(listen_fd, 5) == -1) {
        perror("listen 监听失败");
        return 1;
    }
    fprintf(stderr, "HTTP 服务端启动，监听端口 %d，静态目录 %s/\n", PORT, STATIC_ROOT);

    char* req = (char*)malloc(MAX_REQUEST);
    if (req == NULL) {
        fprintf(stderr, "malloc 失败\n");
        return 1;
    }

    const char* not_found_body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>404 Not Found</title></head>"
        "<body style='font-family:sans-serif;padding:40px'>"
        "<h1>404 Not Found</h1><p>请求的页面不存在</p></body></html>";
    const char* internal_error_body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>500 Internal Server Error</title></head>"
        "<body style='font-family:sans-serif;padding:40px'>"
        "<h1>500 Internal Server Error</h1><p>服务器内部错误</p></body></html>";
    const char* bad_request_body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>400 Bad Request</title></head>"
        "<body style='font-family:sans-serif;padding:40px'>"
        "<h1>400 Bad Request</h1><p>请求格式不对</p></body></html>";

    while (1)
    {
        sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int conn_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &cli_len);
        if (conn_fd == -1) {
            perror("accept 失败");
            continue;
        }

        struct timeval tv;
        tv.tv_sec  = RECV_TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        fprintf(stderr, "客户端接入：%s : %d\n",
                inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        long req_len = 0;
        char* body = NULL;

        // ===== ① 收完整的请求头（直到出现 \r\n\r\n）=====
        while (body == NULL) {
            if (req_len >= MAX_REQUEST - 1) {
                fprintf(stderr, "请求头超过上限\n");
                break;
            }
            ssize_t n = recv(conn_fd, req + req_len, (size_t)(MAX_REQUEST - 1 - req_len), 0);
            if (n <= 0) {
                if (n < 0) {
                    if (errno == EINTR) continue;
                    fprintf(stderr, "recv 头失败: %s\n", strerror(errno));
                }
                break;
            }
            req_len += n;
            req[req_len] = '\0';
            body = find_body_start(req);
        }

        if (body == NULL) {
            fprintf(stderr, "  没有收到完整的请求头（%ld 字节）\n", req_len);
            send_response(conn_fd, "400 Bad Request", "text/html; charset=utf-8",
                          bad_request_body, strlen(bad_request_body));
            close(conn_fd);
            continue;
        }

        fprintf(stderr, "---- 请求头 ----\n");
        fwrite(req, 1, (size_t)(body - req), stderr);
        fprintf(stderr, "\n----------------\n");

        // ===== ② 解析请求行 =====
        char method[16] = {0};
        char path[256]   = {0};
        if (sscanf(req, "%15s %255s", method, path) != 2) {
            send_response(conn_fd, "400 Bad Request", "text/html; charset=utf-8",
                          bad_request_body, strlen(bad_request_body));
            close(conn_fd);
            continue;
        }

        // ===== ③ 收完整的 body =====
        long content_length = 0;
        char* cl = find_ci(req, "Content-Length:");
        if (cl != NULL) {
            content_length = atol(cl + strlen("Content-Length:"));
        }

        if (content_length < 0 ||
            (size_t)(body - req) + (size_t)content_length >= (size_t)MAX_REQUEST) {
            fprintf(stderr, "  Content-Length 不合法或超限: %ld\n", content_length);
            send_response(conn_fd, "413 Payload Too Large", "text/html; charset=utf-8",
                          bad_request_body, strlen(bad_request_body));
            close(conn_fd);
            continue;
        }

        long body_got = req_len - (body - req);

        while (body_got < content_length) {
            ssize_t n = recv(conn_fd, req + req_len, (size_t)(MAX_REQUEST - 1 - req_len), 0);
            if (n <= 0) {
                if (n < 0) {
                    if (errno == EINTR) continue;
                    fprintf(stderr, "recv body 失败: %s\n", strerror(errno));
                }
                break;
            }
            req_len  += n;
            body_got += n;
        }

        fprintf(stderr, "  %s %s | Content-Length=%ld | 收到 body=%ld 字节 | %s\n",
                method, path, content_length, body_got,
                (body_got == content_length) ? "OK" : "不一致");

        // ===== ④ 路由 =====
        if (strcmp(method, "GET") == 0)
        {
            if (strcmp(path, "/history") == 0)
            {
                // 历史查询：返回最近 N 条推理记录的 JSON
                std::string js = db.recent_json(HISTORY_LIMIT);
                fprintf(stderr, "  /history: 返回 %zu 字节 JSON\n", js.size());
                send_response(conn_fd, "200 OK", "application/json; charset=utf-8",
                              js.data(), js.size());
            }
            else
            {
                char resolved[PATH_MAX];
                if (!safe_path(path, resolved, sizeof(resolved))) {
                    send_response(conn_fd, "404 Not Found", "text/html; charset=utf-8",
                                  not_found_body, strlen(not_found_body));
                } else {
                    char* data = NULL;
                    long  len  = 0;
                    if (read_file(resolved, &data, &len)) {
                        send_response(conn_fd, "200 OK", content_type_of(resolved),
                                      data, (size_t)len);
                        free(data);
                    } else {
                        send_response(conn_fd, "500 Internal Server Error",
                                      "text/html; charset=utf-8",
                                      internal_error_body, strlen(internal_error_body));
                    }
                }
            }
        }
        else if (strcmp(method, "POST") == 0)
        {
            if (strcmp(path, "/infer") == 0)
            {
                // 客户端可以用 X-Filename 头带文件名（可选）
                char image_name[256];
                header_value(req, "X-Filename:", image_name, sizeof(image_name));
                if (image_name[0] == '\0') {
                    snprintf(image_name, sizeof(image_name), "upload");
                }

                std::vector<unsigned char> bytes(body, body + body_got);

                // ---- 计时开始 ----
                auto t0 = std::chrono::steady_clock::now();

                cv::Mat blob, orig;
                std::vector<float> mask;
                std::vector<unsigned char> png;
                int fail = 0;   // 0=成功 1=不是图片 2=推理失败 3=编码失败

                if (!preprocess_bytes(bytes, blob, orig))     fail = 1;
                else if (!engine.run(blob, mask))             fail = 2;
                else if (!make_cutout_png(orig, mask, png))   fail = 3;

                auto t1 = std::chrono::steady_clock::now();
                int cost_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                // ---- 计时结束 ----

                if (fail == 1) {
                    fprintf(stderr, "  /infer: 不是有效图片（%ld 字节）\n", body_got);
                    send_response(conn_fd, "400 Bad Request", "text/plain; charset=utf-8",
                                  "不是有效的图片", strlen("不是有效的图片"));
                } else if (fail == 2) {
                    send_response(conn_fd, "500 Internal Server Error",
                                  "text/plain; charset=utf-8",
                                  "推理失败", strlen("推理失败"));
                } else if (fail == 3) {
                    send_response(conn_fd, "500 Internal Server Error",
                                  "text/plain; charset=utf-8",
                                  "生成图片失败", strlen("生成图片失败"));
                } else {
                    // 算内容指纹并入库
                    const std::string hash = sha256_hex(bytes.data(), bytes.size());
                    db.log_infer(image_name, hash, orig.cols, orig.rows, cost_ms);

                    fprintf(stderr, "  /infer: %s | %dx%d | %d ms | PNG %zu 字节 | hash=%.12s…\n",
                            image_name, orig.cols, orig.rows, cost_ms, png.size(), hash.c_str());

                    send_response(conn_fd, "200 OK", "image/png", png.data(), png.size());
                }
            }
            else if (strcmp(path, "/echo") == 0)
            {
                send_response(conn_fd, "200 OK", "text/plain; charset=utf-8",
                              body, (size_t)body_got);
            }
            else
            {
                send_response(conn_fd, "404 Not Found", "text/html; charset=utf-8",
                              not_found_body, strlen(not_found_body));
            }
        }
        else
        {
            send_response(conn_fd, "405 Method Not Allowed", "text/html; charset=utf-8",
                          bad_request_body, strlen(bad_request_body));
        }

        close(conn_fd);
    }

    free(req);
    close(listen_fd);
    return 0;
}
