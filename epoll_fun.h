#include "head.h"
#include "settings.h"

// 检查epoll问价描述符是否成功创建
void epoll_create_check()
{
    if (epoll_fd == -1)
    {
        printf("epoll创建失败\n");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }
}