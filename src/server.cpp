#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define BUF_SIZE 1024
#define PORT 8080

void send_response(int conn_fd, const char* status, const char* body)
{
    char resp[2048];
    int n = snprintf(resp, sizeof(resp),
        "HTTP/1.1 %s\r\n"                    // ← 状态从参数来
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status, strlen(body), body);          // ← status 在这里插进去
    send(conn_fd, resp, n, MSG_NOSIGNAL);
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
            sscanf(buf,"%15s %255s",method,path);
            
            if(strcmp(path,"/")==0)
            {
              send_response(conn_fd,"200 OK",html_body);
            }
            else{
                send_response(conn_fd,"404 NOT FOUND",not_found_body);
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
