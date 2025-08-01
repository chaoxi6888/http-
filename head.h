// 头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <asm-generic/socket.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <sys/sendfile.h> // sendfile头文件
#include <sys/stat.h>     // 使用struct stat的头文件
#include <ctype.h>
#include <time.h>
#include <stdarg.h>