#include "settings.h"

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

// 线程函数
void *work_thread(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&mutex);
        while (task_count == 0)
        {
            pthread_cond_wait(&cond, &mutex);
        }

        Task task = task_queue[--task_count];
        pthread_mutex_unlock(&mutex);

        // 处理任务
        write(task.fd, task.buf, task.len);
        free(task.buf); // 必须释放内存！
    }
    return NULL;
}

void add_task(int fd, char *buf, int len)
{
    pthread_mutex_lock(&mutex);
    if (task_count < 1024)
    {
        // 深度复制数据到堆内存
        char *task_buf = (char *)malloc(len);
        if (task_buf == NULL)
        {
            perror("malloc failed");
            pthread_mutex_unlock(&mutex);
            return;
        }
        memcpy(task_buf, buf, len);

        task_queue[task_count].fd = fd;
        task_queue[task_count].buf = task_buf;
        task_queue[task_count].len = len;
        task_count++;
        pthread_cond_signal(&cond);
    }
    pthread_mutex_unlock(&mutex);
}

int main(int argc, char const *argv[])
{
    // 初始服务器套接字
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_init_check();

    // 设置地址参数（用于绑定）
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 端口复用
    int opt = 1; // 1 启用，0 禁用
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 绑定
    bind_check(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)));

    // 监听
    listen_check(listen(server_fd, 4096));
    printf("服务器启动.....\n");

    // 建立线程池来实现多线程
    pthread_t pthreadpools[PTHREADMAX];
    for (int i = 0; i < PTHREADMAX; i++)
    {
        pthread_create(&pthreadpools[i], NULL, work_thread, NULL);
    }

    // 建立epoll实例
    epoll_fd = epoll_create1(0);
    epollcreate_check();

    struct epoll_event event;
    event.events = EPOLLIN; // LT 模式（默认）
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1)
    {
        perror("服务器文件描述符epctl失败");
        return EXIT_FAILURE;
    }

    struct epoll_event evs[MAXEVENTS];
    while (1)
    {
        int events_num = epoll_wait(epoll_fd, evs, MAXEVENTS, -1);
        epollwait_check(events_num);
        for (int i = 0; i < events_num; i++)
        {
            int fd = evs[i].data.fd;
            if (fd == server_fd)
            {
                // 服务端只要建立连接即可
                struct sockaddr_in caddr;
                caddr.sin_family = AF_INET;
                socklen_t c_len = sizeof(caddr);
                int c_fd = accept(server_fd, (struct sockaddr *)&caddr, &c_len);
                fcntl(c_fd, F_SETFL, O_NONBLOCK); // 必须设置为非阻塞！
                // 将产生的客户端文件描述符加入epoll实例
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = c_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, c_fd, &ev);
            }
            else
            {
                // 客户端
                // 边缘触发模式必须循环读取！
                // 修改读取循环
                while (1)
                {
                    char buf[BUFSIZE]; // 改为栈上分配，避免内存管理问题
                    int n = read(fd, buf, BUFSIZE);
                    if (n > 0)
                    {
                        char *task_buf = malloc(n);
                        memcpy(task_buf, buf, n);
                        add_task(fd, task_buf, n);
                    }
                    else if (n == 0)
                    {
                        close(fd);
                        break;
                    }
                    else if (errno == EAGAIN)
                    {
                        break; // ET模式下数据已读完
                    }
                    else
                    {
                        perror("read error");
                        close(fd);
                        break;
                    }
                }
            }
        }
    }

    // 关闭服务器
    close(epoll_fd);
    close(server_fd);
    return EXIT_SUCCESS;
}
