#include "net_func.h" // 导入网络层相关函数头文件

int main(int argc, char const *argv[])
{
    // 初始服务器套接字
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_init_check(server_fd);

    // 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = 8080;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_check(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)), server_fd);

    // 监听
    listen_check(listen(server_fd, 4096), server_fd);
    printf("服务器启动.....\n");

    return EXIT_SUCCESS;
}
