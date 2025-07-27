#include "net_func.h" // 导入网络层相关函数头文件
#include "settings.h" // 导入宏
#include "pthd_fun.h" // 导入线程相关函数

int main(int argc, char const *argv[])
{
    // 初始服务器套接字
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_init_check();

    // 设置地址参数（用于绑定）
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = 8080;
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
    epoll_create_check();

    // 关闭服务器
    close(server_fd);
    return EXIT_SUCCESS;
}
