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

// 根据文件扩展名获取Content-Type
const char *get_content_type(const char *filename)
{
    const char *dot = strrchr(filename, '.');
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

// URL解码函数
void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src)
    {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b)))
        {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
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

// // 解析HTTP请求，获取请求的文件路径
// int parse_http_request(const char *request, char *filename, size_t max_len)
// {
//     // 简单的解析：查找第一个空格和第二个空格之间的内容
//     const char *start = strchr(request, ' ');
//     if (!start)
//         return -1;

//     start++; // 跳过第一个空格
//     const char *end = strchr(start, ' ');
//     if (!end)
//         return -1;

//     size_t len = end - start;
//     if (len >= max_len)
//         return -1;

//     memcpy(filename, start, len);
//     filename[len] = '\0';

//     // 如果请求的是根目录，返回默认文件
//     if (strcmp(filename, "/") == 0)
//     {
//         strncpy(filename, "/login.html", max_len);
//     }

//     // 去掉开头的斜杠，因为我们要从当前目录查找文件
//     if (filename[0] == '/')
//     {
//         memmove(filename, filename + 1, strlen(filename));
//     }

//     return 0;
// }

// 修改后的解析函数
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

    // 去掉开头的斜杠（保留music/这样的前缀）
    if (filename[0] == '/')
    {
        memmove(filename, filename + 1, strlen(filename));
    }

    return 0;
}