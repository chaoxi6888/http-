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
// 检查epollwait是否出错
void epollwait_check(int evnum);
// 根据文件扩展名获取Content-Type
const char *get_content_type(const char *filename);
// 解析HTTP请求，获取请求的文件路径
int parse_http_request(const char *request, char *filename, size_t max_len);
// 线程安全日志函数，带时间戳
void write_log(const char *fmt, ...);
// 添加任务函数并激活线程
void add_task(int fd, char *buf, int len);
// 客户端处理函数
void handle_client_event(int fd);
// 线程函数
void *work_thread(void *arg);

// 主函数
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
    listen_check(listen(server_fd, LISTENMAX));
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
        int events_num = epoll_wait(epoll_fd, evs, MAXEVENTS, -1); // 阻塞等待
        epollwait_check(events_num);
        for (int i = 0; i < events_num; i++)
        {
            int fd = evs[i].data.fd;
            if (fd == server_fd)
            {
                // 服务端只要建立连接即可
                struct sockaddr_in caddr;
                socklen_t c_len = sizeof(caddr);
                int c_fd = accept(server_fd, (struct sockaddr *)&caddr, &c_len);
                // 检查accept是否成功连接
                if (c_fd == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    perror("accept 失败");
                    break;
                }
                fcntl(c_fd, F_SETFL, O_NONBLOCK); // 必须设置为非阻塞IO
                write_log("新的客户端%d连接%s:%d\n", c_fd, inet_ntoa(caddr.sin_addr), htons(caddr.sin_port));
                // 由于 X-Forwarded-For 只在 HTTP 请求头里，accept 阶段还拿不到
                // 所以这里只能记录代理的IP（如127.0.0.1），无法直接获得真实IP
                // 保存客户端信息
                client_infos[c_fd].fd = c_fd;
                strcpy(client_infos[c_fd].ip, inet_ntoa(caddr.sin_addr));
                client_infos[c_fd].port = ntohs(caddr.sin_port);
                // 将产生的客户端文件描述符加入epoll实例
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = c_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, c_fd, &ev);
            }
            else
            {
                // 客户端事件处理函数
                handle_client_event(fd);
            }
        }
    }

    // 关闭服务器
    close(epoll_fd);
    close(server_fd);
    // 清理线程池
    server_running = 0;
    pthread_cond_broadcast(&cond);
    for (int i = 0; i < PTHREADMAX; i++)
    {
        pthread_join(pthreadpools[i], NULL);
    }

    return EXIT_SUCCESS;
}