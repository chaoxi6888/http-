#include "settings.h"

// 检查服务套接字是否成功创建
void server_init_check();
// 初始化服务套接字
struct sockaddr_in bind_init(struct sockaddr_in addr);
// 检查是否成功绑定地址
void bind_check(int re);
// 检查是否成功能监听
void listen_check(int re);
// 检查epoll问价描述符是否成功创建
void epollcreate_check();
// 检查opollwait是否出错
void epollwait_check(int evnum);
// 根据文件扩展名获取Content-Type
const char *get_content_type(const char *filename);
// 解析HTTP请求，获取请求的文件路径
int parse_http_request(const char *request, char *filename, size_t max_len);

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

        // 解析HTTP请求
        char filename[256] = {0};
        if (parse_http_request(task.buf, filename, sizeof(filename)) != 0)
        {
            const char *bad_request =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 15\r\n"
                "Connection: close\r\n\r\n"
                "Bad Request";
            write(task.fd, bad_request, strlen(bad_request));
            close(task.fd);
            free(task.buf);
            continue;
        }

        // 打开请求的文件
        // int file_fd = open(filename, O_RDONLY);
        // if (file_fd == -1)
        // {
        //     // 文件不存在时返回404
        //     const char *not_found =
        //         "HTTP/1.1 404 Not Found\r\n"
        //         "Content-Type: text/plain\r\n"
        //         "Content-Length: 13\r\n"
        //         "Connection: close\r\n\r\n"
        //         "File not found";
        //     write(task.fd, not_found, strlen(not_found));
        //     close(task.fd);
        //     free(task.buf);
        //     continue;
        // }

        char fullpath[512] = {0};
        snprintf(fullpath, sizeof(fullpath), "%s", filename); // 保留原始路径结构

        int file_fd = open(fullpath, O_RDONLY);
        if (file_fd == -1)
        {
            perror("open failed"); // 添加详细错误输出
            printf("尝试打开的文件路径: %s\n", fullpath);
            // ... 返回404 ...
            const char *not_found =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 13\r\n"
                "Connection: close\r\n\r\n"
                "File not found";
            write(task.fd, not_found, strlen(not_found));
            close(task.fd);
            free(task.buf);
            continue;
        }

        struct stat file_stat;
        if (fstat(file_fd, &file_stat) == -1)
        {
            perror("获取被请求文件失败");
            close(file_fd);
            close(task.fd);
            free(task.buf);
            continue; // 不终止线程
        }

        // 获取Content-Type
        const char *content_type = get_content_type(filename);

        char response_header[512];
        int header_len = snprintf(response_header, sizeof(response_header),
                                  "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: %s\r\n"
                                  "Content-Length: %ld\r\n"
                                  "Connection: close\r\n\r\n",
                                  content_type, file_stat.st_size);

        // 发送响应头
        ssize_t sent = write(task.fd, response_header, header_len);
        if (sent != header_len)
        {
            perror("发送响应头失败");
            close(file_fd);
            close(task.fd);
            free(task.buf);
            continue;
        }

        // 分块发送文件内容（优化大文件传输）
        off_t offset = 0;
        size_t remaining = file_stat.st_size;
        const size_t CHUNK_SIZE = 1 << 20; // 1MB分块 2^20B = 1MB

        while (remaining > 0)
        {
            size_t send_size = MIN(CHUNK_SIZE, remaining);
            ssize_t bytes_sent = sendfile(task.fd, file_fd, &offset, send_size);

            if (bytes_sent <= 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 网络缓冲区满，短暂等待后重试
                    usleep(10000);
                    continue;
                }
                perror("文件发送失败");
                break;
            }

            remaining -= bytes_sent;
        }

        close(file_fd);
        close(task.fd);
        free(task.buf);
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
            perror("add_task,malloc分配失败\n");
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
    addr = bind_init(addr);

    // 端口复用
    int opt = 1; // 1 启用，0 禁用
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 绑定
    bind_check(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)));

    // 监听
    listen_check(listen(server_fd, 4096));
    printf("服务器在%d端口上启动.....\n", PROT);

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
                printf("新的客户端连接%s:%d\n", inet_ntoa(caddr.sin_addr), htons(caddr.sin_port));
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
                        perror("客户端读取失败\n");
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
