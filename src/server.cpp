#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include<fcntl.h>
#include<sys/stat.h>
#define BUF_SIZE 1024
#define PORT 8080

// 把数据完整发出去（处理"短写"）
// send 的返回值是实际发出的字节数，不保证一次发完；
// 数据大时内核 socket 缓冲区会满，所以要循环直到全部发完。
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

// 发送 HTTP 响应
//   body 是 void*（不是 char*）—— 因为要能发二进制（图片）
//   body_len 必须显式传 —— 因为二进制数据里可能包含 '\0'，不能用 strlen
void send_response(int conn_fd, const char* status, const char* content_type,
                   const void* body, size_t body_len)
{
    // 只拼头部
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

    // 先发头，再发 body 不能把二进制塞进 %s 里拼
    send_all(conn_fd, header, (size_t)n);
    if (body != NULL && body_len > 0) {
        send_all(conn_fd, body, body_len);
    }
}

// 把整个文件读进内存
// 成功返回 true，*out 指向 malloc 出来的缓冲区，*out_len 是字节数
// 调用者负责 free(*out)
static bool read_file(const char* filename, char** out, long* out_len)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return false;                    // 文件不存在 / 没权限
    }

    struct stat st;//stat类型是文件元信息 fstat函数把fd对应的文件中的元信息存入stat
    if (fstat(fd, &st) < 0) {
        close(fd);
        return false;
    }

    long size = st.st_size;//文件大小
    char* data = (char*)malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL) 
    {
        close(fd);
        return false;//分配失败
    }
    
    long got=0;
    while(got<size)
    {
        ssize_t r = read(fd, data + got, (size_t)(size - got));
        if(r<0)
        {
            if(errno==EINTR) continue;
            fprintf(stderr, "send 失败: %s\n", strerror(errno));
            close(fd);
            free(data);
            return false;
        }
        if(r==0) break;
        got+=r;
    }
    // 循环 read，直到读满 size 字节
    // read 也可能只读一部分
    // r < 0出错；r == 0 文件提前结束
    close(fd);
    *out = data;
    *out_len = got;
    return true;
}
int main()
{
    //创建socket
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);//IPv4和TCP协议
    if(listen_fd==-1)
    {
        perror("socket创建失败");
        exit(1);
    }
    //地址复用
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //设置SO_REUSEADDR时opt固定为1
    //填充地址结构
    struct sockaddr_in serv_addr;
    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;            // 地址族：IPv4
    serv_addr.sin_port = htons(PORT);          // 端口号：转网络字节序（大端）
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定本机所有网卡IP
    //绑定IP 端口
    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        perror("bind 绑定失败");
        exit(1);
    }
    //开始监听
    if (listen(listen_fd, 5) == -1)
    {
        perror("listen 监听失败"); exit(1); //5表示全连接队列最大长度
    }
    fprintf(stderr, "HTTP 服务端启动，监听端口 %d...\n", PORT);

    //要返回的 HTML 页面
    const char* html_body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>imgsvc</title></head>"
        "<body style='font-family:sans-serif;padding:40px'>"
        "<h1>imgsvc</h1>"
        "<p>图像抠图服务 · 运行中</p>"
        "</body></html>";
    const char* not_found_body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>404 Not Found</title></head>"
        "<body style='font-family:sans-serif;padding:40px'>"
        "<h1>404 Not Found</h1>"
        "<p>请求的页面不存在</p>"
        "</body></html>";

    //循环接收客户端连接
    while(1)
    {
        sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);//保存客户端地址信息
        // 阻塞等待客户端连接，成功返回新的连接套接字
        int conn_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &cli_len);
        if (conn_fd == -1)
        {
            perror("accept 失败");
             continue;
        }
        fprintf(stderr, "客户端接入：%s : %d\n",
                inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        //接收数据
        char buf[BUF_SIZE] = {0};
        ssize_t recv_len = recv(conn_fd, buf, BUF_SIZE - 1, 0);
        if (recv_len > 0)
        {
            //把客户端发来的原始请求打印出来
            fprintf(stderr, "---- 收到请求 ----\n%s\n------------------\n", buf);

            char method[16]={0};
            char path[256]={0};
            sscanf(buf,"%15s %255s",method,path); //将缓存中get和路径读入

            if(strcmp(path,"/")==0)
            {
                char*data=NULL;
                long len=0;
                if(read_file("static/index.html", &data, &len))
                {
                    send_response(conn_fd, "200 OK", "text/html; charset=utf-8",
                              data, size_t(len));
                              free(data);
                }
                else{
                    send_response(conn_fd, "404 Not Found", "text/html; charset=utf-8",
                              not_found_body, strlen(not_found_body));
                }
            }
            else
            {
                send_response(conn_fd, "404 Not Found", "text/html; charset=utf-8",
                              not_found_body, strlen(not_found_body));
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

        //关闭连接套接字（短连接）
        close(conn_fd);
    }
    close(listen_fd);
    return 0;
}
