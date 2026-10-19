#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024
#define PORT 8080
#define STATIC_ROOT "static"

// ---------- 发数据 ----------

// 把数据完整发出去
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

// 发送 HTTP 响应（二进制安全：body 用 void* + 显式长度）
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

// 把整个文件读进内存。成功返回 true，*out 由调用者 free
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
        if (r == 0) break;              // 文件比 fstat 说的短
        got += r;
    }

    close(fd);
    *out = data;
    *out_len = got;                     // 用实际读到的字节数
    return true;
}

// ---------- 路径安全 ----------

// 把请求路径安全地映射到 static/ 下的文件
//   1.把路径规范化成绝对路径（realpath 会解析掉所有 ../ 和符号链接）
//   2.检查规范化后的结果是否还在 static 目录里面
//
// 成功返回 true，out 收到规范化后的绝对路径；否则 false
static bool safe_path(const char* path, char* out, size_t out_size)
{
    // static 目录的绝对路径只算一次
    static char static_root[PATH_MAX];
    static int  inited = 0;
    if (!inited) {
        if (realpath(STATIC_ROOT, static_root) == NULL)  //realpath函数把相对路径改为绝对路径
        {
            fprintf(stderr, "找不到静态目录 %s: %s\n", STATIC_ROOT, strerror(errno));
            return false;
        }
        inited = 1;
    }

    //拼成 static/xxx
    char joined[1024];
    if (strcmp(path, "/") == 0) {
        snprintf(joined, sizeof(joined), "%s/index.html", STATIC_ROOT);
    } else {
        snprintf(joined, sizeof(joined), "%s%s", STATIC_ROOT, path);
    }

    //规范化：解析掉 ../ 和符号链接
    char resolved[PATH_MAX];
    if (realpath(joined, resolved) == NULL) {
        return false;                   // 文件不存在 / 没权限
    }

    //校验：必须还在 static_root 里面
    size_t root_len = strlen(static_root);
    if (strncmp(resolved, static_root, root_len) != 0) {
        return false;                   // 跳出 static 了
    }
    // 前缀校验的问题：必须确认后面紧跟 '/' 或结束，
    //   否则 static_bak/xxx 也会被判定为"以 static 开头"而放过
    if (resolved[root_len] != '\0' && resolved[root_len] != '/') {
        return false;
    }

    snprintf(out, out_size, "%s", resolved);
    return true;
}

// ---------- Content-Type ----------

static const char* content_type_of(const char* filename)
{
    const char* dot = strrchr(filename, '.');//从字符串尾部找第一个. 就是拿到后缀起始地址
    if (dot == NULL) return "application/octet-stream";//返回二进制流
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";//网页
    if (strcmp(dot, ".css")  == 0) return "text/css; charset=utf-8";//样式表
    if (strcmp(dot, ".js")   == 0) return "application/javascript; charset=utf-8";//JS脚本
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";//json数据
    if (strcmp(dot, ".txt")  == 0) return "text/plain; charset=utf-8";//文本
    if (strcmp(dot, ".png")  == 0) return "image/png";//png图片
    if (strcmp(dot, ".jpg")  == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";//jpg图片
    if (strcmp(dot, ".svg")  == 0) return "image/svg+xml";//矢量图
    if (strcmp(dot, ".ico")  == 0) return "image/x-icon";//网站图标
    return "application/octet-stream";
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

    const char* not_found_body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>404 Not Found</title></head>"
        "<body style='font-family:sans-serif;padding:40px'>"
        "<h1>404 Not Found</h1>"
        "<p>请求的页面不存在</p>"
        "</body></html>";
    const char* internal_error_body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>500 Internal Server Error</title></head>"
        "<body style='font-family:sans-serif;padding:40px'>"
        "<h1>500 Internal Server Error</h1>"
        "<p>服务器内部错误</p>"
        "</body></html>";

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
        fprintf(stderr, "客户端接入：%s : %d\n",
                inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        char buf[BUF_SIZE] = {0};
        ssize_t recv_len = recv(conn_fd, buf, BUF_SIZE - 1, 0);
        if (recv_len > 0)
        {
            fprintf(stderr, "---- 收到请求 ----\n%s\n------------------\n", buf);

            char method[16] = {0};
            char path[256]   = {0};
            sscanf(buf, "%15s %255s", method, path);

            // 只接受 GET
            if (strcmp(method, "GET") != 0) 
            {
                send_response(conn_fd, "405 Method Not Allowed",
                              "text/html; charset=utf-8",
                              not_found_body, strlen(not_found_body));
            } else {
                char resolved[PATH_MAX];
                if (!safe_path(path, resolved, sizeof(resolved))) 
                {
                    // 文件不存在，或者试图跳出 static 目录
                    fprintf(stderr, "  拒绝路径: %s\n", path);
                    send_response(conn_fd, "404 Not Found",
                                  "text/html; charset=utf-8",
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
        else if (recv_len == 0)
        {
            fprintf(stderr, "客户端正常关闭连接\n");
        }
        else
        {
            perror("recv 出错");
        }

        close(conn_fd);
    }
    close(listen_fd);
    return 0;
}
