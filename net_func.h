#include "head.h" // 导入项目总头文件

// 检查服务套接字是否成功创建
void server_init_check(int s_fd)
{
    if (s_fd == -1)
    {
        perror("服务器套接字初始化失败");
        exit(EXIT_FAILURE);
    }
}

// 检查是否成功绑定地址
void bind_check(int re, int s_fd)
{
    if (re == -1)
    {
        perror("绑定失败");
        close(s_fd);
        exit(EXIT_FAILURE);
    }
}

// 检查是否成功能监听
void listen_check(int re, int s_fd)
{
    if (re == -1)
    {
        perror("监听启动失败");
        close(s_fd);
        exit(EXIT_FAILURE);
    }
}