#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <arpa/inet.h>
#include <memory.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {

    pid_t recordInFile;
    int table = open("./test.txt", O_CREAT | O_RDWR, 777);

    char logMessage[200];
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    sprintf(logMessage, "Time: %d, Info: child created. PID: %d\n",start.tv_sec , 2137);
    printf("%s\n",logMessage);
    write(table, logMessage, sizeof(logMessage));


}
