#include "head.h"

#define PTHREADMAX 10                     // 线程池子中最大数量
#define MAXEVENTS 1024                    // epoll实例中事件的最大数量
#define BUFSIZE 1024                      // 缓存区大小
#define PROT 9000                         // 服务器端口号
#define MIN(x, y) ((x) < (y) ? (x) : (y)) // 定义MIN宏

int server_fd; // 服务器套接字的文件描述符
int epoll_fd;  // epoll的文件描述符

typedef struct
{
    int fd;
    char *buf;
    int len;
} Task;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
Task task_queue[1024];
int task_count = 0;

// 检查服务套接字是否成功创建
void server_init_check()
{
    if (server_fd == -1)
    {
        perror("服务器套接字初始化失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
}

// 初始化服务套接字
struct sockaddr_in bind_init(struct sockaddr_in addr)
{
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    return addr;
}

// 检查是否成功绑定地址
void bind_check(int re)
{
    if (re == -1)
    {
        perror("绑定失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
}

// 检查是否成功能监听
void listen_check(int re)
{
    if (re == -1)
    {
        perror("监听启动失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
}

// 检查epoll问价描述符是否成功创建
void epollcreate_check()
{
    if (epoll_fd == -1)
    {
        printf("epoll创建失败\n");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }
}

// 检查opollwait是否出错
void epollwait_check(int evnum)
{
    if (evnum == -1)
    {
        perror("epollwait出错");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }
}