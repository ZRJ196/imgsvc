#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>        // strncasecmp
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024
#define PORT 8080
#define STATIC_ROOT "static"

// 单个请求的最大字节数（头 + body）。
// 注意：不能按 Content-Length 去 malloc ——恶意客户端谎报一个巨大值会耗尽内存。
#define MAX_REQUEST (10 * 1024 * 1024)

// recv 超时（秒）。防止"客户端连上来了但一个字不发"把单线程服务器卡死。
#define RECV_TIMEOUT_SEC 5

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

// 大小写不敏感地在 haystack 里找 needle（HTTP 头名是大小写不敏感的）
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

// 在 req（长度 req_len，已 NUL 结尾）里找头部结束标记 \r\n\r\n
// 找到返回 body 的起始指针，找不到返回 NULL
static char* find_body_start(char* req)
{
    char* sep = strstr(req, "\r\n\r\n");
    return (sep == NULL) ? NULL : sep + 4;
}

// ---------- main ----------

int main()
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("socket创建失败");
        exit(1);
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        perror("bind 绑定失败");
        exit(1);
    }
    if (listen(listen_fd, 5) == -1)
    {
        perror("listen 监听失败");
        exit(1);
    }
    fprintf(stderr, "HTTP 服务端启动，监听端口 %d，静态目录 %s/\n", PORT, STATIC_ROOT);

    // 请求缓冲区只分配一次，每个连接复用（避免每请求 malloc 10MB）
    char* req = (char*)malloc(MAX_REQUEST);
    if (req == NULL) {
        fprintf(stderr, "malloc 失败\n");
        exit(1);
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
        if (conn_fd == -1)
        {
            perror("accept 失败");
            continue;
        }

        // 设置接收超时：客户端不发数据时 recv 不会永远阻塞
        struct timeval tv;
        tv.tv_sec  = RECV_TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        fprintf(stderr, "客户端接入：%s : %d\n",
                inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        long req_len = 0;
        char* body = NULL;

        // ===== ① 收完整的【请求头】（直到出现 \r\n\r\n）=====
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
            req[req_len] = '\0';            // 保持 NUL 结尾，strstr 才能安全扫描
            body = find_body_start(req);
        }

        if (body == NULL) {
            // 没收到完整的头：客户端断开 / 超时 / 请求非法
            fprintf(stderr, "  没有收到完整的请求头（%ld 字节）\n", req_len);
            send_response(conn_fd, "400 Bad Request", "text/html; charset=utf-8",
                          bad_request_body, strlen(bad_request_body));
            close(conn_fd);
            continue;
        }

        // 头部日志（用 fwrite 显式长度，body 可能是二进制）
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

        // ===== ③ 解析 Content-Length，把【整个 body】收完 =====
        long content_length = 0;
        char* cl = find_ci(req, "Content-Length:");
        if (cl != NULL) {
            content_length = atol(cl + strlen("Content-Length:"));
        }

        if (content_length < 0 || (size_t)(body - req) + (size_t)content_length >= (size_t)MAX_REQUEST) {
            fprintf(stderr, "  Content-Length 不合法或超限: %ld\n", content_length);
            send_response(conn_fd, "413 Payload Too Large", "text/html; charset=utf-8",
                          bad_request_body, strlen(bad_request_body));
            close(conn_fd);
            continue;
        }

        long body_got = req_len - (body - req);     // 第一次 recv 已经收到多少 body

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

        // ★ 关键验证：收到的 body 字节数必须等于 Content-Length
        fprintf(stderr, "  %s %s | Content-Length=%ld | 实际收到 body=%ld 字节 | %s\n",
                method, path, content_length, body_got,
                (body_got == content_length) ? "✅ 一致" : "❌ 不一致");

        // ===== ④ 路由 =====
        if (strcmp(method, "GET") == 0)
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
        else if (strcmp(method, "POST") == 0)
        {
            // 第 5 步：先把 body 原样返回，用来验证"收全了"
            // 下一步会在这里接 /infer
            send_response(conn_fd, "200 OK", "text/plain; charset=utf-8",
                          body, (size_t)body_got);
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
