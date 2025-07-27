// 宏参数
#define PTHREADMAX 10 // 线程池子中最大数量

int server_fd; // 服务器套接字的文件描述符
int epoll_fd;  // epoll的文件描述符