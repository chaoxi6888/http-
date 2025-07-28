#include "head.h"

#define PTHREADMAX 10  // 线程池子中最大数量
#define MAXEVENTS 1024 // epoll实例中事件的最大数量
#define BUFSIZE 1024   // 缓存区大小

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