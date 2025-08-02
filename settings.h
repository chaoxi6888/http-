#include "head.h"
#define LISTENMAX 4096
#define PTHREADMAX 4                      // 线程池子中最大数量
#define MAXEVENTS 100                     // epoll实例中事件的最大数量
#define BUFSIZE 1024                      // 缓存区大小
#define MAX_CLIENTS 1024                  // 最大客户端的数量
#define PROT 9000                         // 服务器端口号
#define MIN(x, y) ((x) < (y) ? (x) : (y)) // 定义MIN宏
volatile int server_running = 1;          // 全局标志，比int更安全

int server_fd; // 服务器套接字的文件描述符
int epoll_fd;  // epoll的文件描述符

typedef struct
{
    int fd;
    char *buf;
    int len;
} Task;

typedef struct
{
    int fd;
    char ip[INET_ADDRSTRLEN];
    int port;
} ClientInfo; // 客户端信息

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; // 日志锁
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
Task task_queue[1024];
int task_count = 0;
ClientInfo client_infos[MAX_CLIENTS];

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

// 根据文件扩展名获取Content-Type
const char *get_content_type(const char *filename)
{
    const char *dot = strrchr(filename, '.'); // 最后一个.后面的是文件后缀
    if (!dot)
        return "application/octet-stream";

    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html";
    if (strcmp(dot, ".txt") == 0)
        return "text/plain";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".js") == 0)
        return "application/javascript";
    if (strcmp(dot, ".json") == 0)
        return "application/json";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".ico") == 0)
        return "image/x-icon";
    if (strcmp(dot, ".pdf") == 0)
        return "application/pdf";
    if (strcmp(dot, ".zip") == 0)
        return "application/zip";

    return "application/octet-stream";
}

// 十六进制数字转数字
int hex_to_dec(char c)
{
    c = toupper(c);
    return (c >= 'A') ? (c - 'A' + 10) : (c - '0');
}

// URL解码函数
void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src)
    {
        if ((*src == '%') &&                  // 当前字符是 '%'
            ((a = src[1]) && (b = src[2])) && // 后面至少有两个字符
            (isxdigit(a) && isxdigit(b)))     // 这两个字符是十六进制数字（0-9, A-F, a-f）
        {
            a = hex_to_dec(a);
            b = hex_to_dec(b);
            *dst++ = 16 * a + b;
            src += 3;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

// 解析函数
int parse_http_request(const char *request, char *filename, size_t max_len)
{
    const char *start = strchr(request, ' ');
    if (!start)
        return -1;

    start++;
    const char *end = strchr(start, ' ');
    if (!end)
        return -1;

    size_t len = end - start;
    if (len >= max_len)
        return -1;

    char encoded[256];
    memcpy(encoded, start, len);
    encoded[len] = '\0';

    // URL解码
    url_decode(filename, encoded);

    // 处理根路径请求
    if (strcmp(filename, "/") == 0)
    {
        strncpy(filename, "login.html", max_len);
        return 0;
    }

    // 去掉开头的斜杠
    if (filename[0] == '/')
    {
        memmove(filename, filename + 1, strlen(filename));
    }

    return 0;
}

// 获取ip地址
char *get_real_ip(const char *request, const char *default_ip)
{
    static char ipbuf[64];
    const char *p = strstr(request, "X-Forwarded-For:");
    if (p)
    {
        p += strlen("X-Forwarded-For:");
        while (*p == ' ')
            p++; // 跳过空格
        int i = 0;
        while (*p && *p != '\r' && *p != '\n' && *p != ',' && i < 63)
        {
            ipbuf[i++] = *p++;
        }
        ipbuf[i] = '\0';
        return ipbuf;
    }
    return (char *)default_ip;
}

// 线程安全日志函数，带时间戳
void write_log(const char *fmt, ...)
{
    pthread_mutex_lock(&log_mutex);
    FILE *fp = fopen("log.txt", "a");
    if (fp)
    {
        // 获取当前时间
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        fprintf(fp, "[%s] ", time_buf);

        va_list args;
        va_start(args, fmt);
        vfprintf(fp, fmt, args);
        va_end(args);

        fclose(fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

// 添加任务函数并激活线程
void add_task(int fd, char *buf, int len)
{
    pthread_mutex_lock(&mutex);
    if (task_count < 1024)
    {
        // 直接保存主线程传进来的buf指针
        task_queue[task_count].fd = fd;
        task_queue[task_count].buf = buf;
        task_queue[task_count].len = len;
        task_count++;
        pthread_cond_signal(&cond);
    }
    pthread_mutex_unlock(&mutex);
}

// 客户端处理函数
void handle_client_event(int fd)
{
    while (1)
    {
        char buf[BUFSIZE]; // 改为栈上分配，避免内存管理问题
        int n = read(fd, buf, BUFSIZE);
        if (n > 0)
        {
            char *task_buf = malloc(n); // 分配新内存将内容传给线程
            memcpy(task_buf, buf, n);
            add_task(fd, task_buf, n);
        }
        else if (n == 0) // 优雅关闭
        {
            printf("客户端 %s:%d (fd=%d) 已断开连接\n",
                   client_infos[fd].ip, client_infos[fd].port, fd);
            struct epoll_event ev;
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev); // 及时移除
            // 不要close(fd)，由线程池关闭
            break;
        }
        else if (errno == EAGAIN)
        {
            break; // ET模式下数据已读完
        }
        else // 大部分关闭
        {
            printf("客户端 %s:%d (fd=%d) 已断开连接,errno=%d\n",
                   client_infos[fd].ip, client_infos[fd].port, fd, errno);
            struct epoll_event ev;
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev); // 及时移除
            // 不要close(fd)，由线程池关闭
            break;
        }
    }
}

// 将客户端fd释放的函数
void cfd_free(int cfd)
{
    struct epoll_event ev;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cfd, &ev);
    close(cfd);
}

// 线程函数
void *work_thread(void *arg)
{
    while (server_running)
    {
        pthread_mutex_lock(&mutex); // 因为task_count 发生改变所以上锁
        while (task_count == 0 && server_running)
        {
            pthread_cond_wait(&cond, &mutex);
        }
        if (!server_running)
        {
            pthread_mutex_unlock(&mutex);
            break;
        }

        Task task = task_queue[--task_count]; // 结构体赋值
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
            // 在线程池 work_thread 里 close(task.fd) 前加
            const char *real_ip = get_real_ip(task.buf, client_infos[task.fd].ip);
            write_log("客户端 %s:%d (fd=%d) 已断开连接（线程池）\n",
                      real_ip, client_infos[task.fd].port, task.fd);
            cfd_free(task.fd);
            free(task.buf); // 释放掉主线程中分配的空间
            continue;
        }

        char fullpath[512] = {0};
        snprintf(fullpath, sizeof(fullpath), "%s", filename);

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
            // 在线程池 work_thread 里 close(task.fd) 前加
            const char *real_ip = get_real_ip(task.buf, client_infos[task.fd].ip);
            write_log("客户端 %s:%d (fd=%d) 已断开连接（线程池）\n",
                      real_ip, client_infos[task.fd].port, task.fd);
            cfd_free(task.fd);
            free(task.buf);
            continue;
        }

        struct stat file_stat;
        if (fstat(file_fd, &file_stat) == -1)
        {
            perror("获取被请求文件信息失败");
            close(file_fd);
            // 在线程池 work_thread 里 close(task.fd) 前加
            const char *real_ip = get_real_ip(task.buf, client_infos[task.fd].ip);
            write_log("客户端 %s:%d (fd=%d) 已断开连接（线程池）\n",
                      real_ip, client_infos[task.fd].port, task.fd);
            cfd_free(task.fd);
            free(task.buf);
            continue; // 不终止线程
        }

        // 获取Content-Type
        const char *content_type = get_content_type(filename);

        char response_header[512];
        int header_len = snprintf(response_header, sizeof(response_header),
                                  "HTTP/1.1 200 OK\r\n"     // HTTP 状态码（表示成功），状态码的文本描述
                                  "Content-Type: %s\r\n"    // 媒体类型
                                  "Content-Length: %ld\r\n" // 响应体的字节大小
                                  "Connection: close\r\n\r\n",
                                  content_type, file_stat.st_size);

        // 发送响应头
        ssize_t sent = write(task.fd, response_header, header_len);
        if (sent != header_len)
        {
            perror("发送响应头失败");
            close(file_fd);
            // 在线程池 work_thread 里 close(task.fd) 前加
            const char *real_ip = get_real_ip(task.buf, client_infos[task.fd].ip);
            write_log("客户端 %s:%d (fd=%d) 已断开连接（线程池）\n",
                      real_ip, client_infos[task.fd].port, task.fd);
            cfd_free(task.fd);
            free(task.buf);
            continue;
        }

        // 分块发送文件内容（优化大文件传输）
        off_t offset = 0;
        size_t remaining = file_stat.st_size;
        const size_t CHUNK_SIZE = 1 << 21; // 1MB分块 2^21B = 10MB

        while (remaining > 0)
        {
            size_t send_size = MIN(CHUNK_SIZE, remaining);                       // 本次要发送的大小
            ssize_t bytes_sent = sendfile(task.fd, file_fd, &offset, send_size); // 实际发送大小

            if (bytes_sent <= 0)
            {
                // EWOULDBLOCK 表示“如果是阻塞IO，这次操作会阻塞；但现在是非阻塞IO，所以直接返回错误”。
                if (errno == EAGAIN || errno == EWOULDBLOCK) // 2.非阻塞IO操作本来会阻塞，但现在直接返回错误。
                {
                    // 网络缓冲区满，短暂等待后重试
                    usleep(10000);
                    printf("文件发送失败，再次尝试.....\n");
                    continue;
                }
                printf("文件发送失败\n");
                break;
            }

            remaining -= bytes_sent;
        }

        close(file_fd);
        // 在线程池 work_thread 里 close(task.fd) 前加
        const char *real_ip = get_real_ip(task.buf, client_infos[task.fd].ip);
        write_log("客户端 %s:%d (fd=%d) 已断开连接（线程池）\n",
                  real_ip, client_infos[task.fd].port, task.fd);
        cfd_free(task.fd);
        free(task.buf);
    }
    return NULL;
}
