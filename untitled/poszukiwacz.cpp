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

void Error(const char* errormsg){
    perror(errormsg);
    exit(11);
}

void Check(int value, const char * errormsg){
    if (value == -1)
        Error(errormsg);
}

long ParseUnit(const char* unit){
    if (strcmp(unit, "Ki") == 0) {
        return 1024;
    } else if (strcmp(unit, "Mi") == 0){
        return 1024 * 1024;
    }
    return 1;
}

long ParseParam(const char* param){
    char *ptr;
    long ret;

    ret = strtol(param, &ptr, 10);
    return ret * ParseUnit(ptr);
}

short GetRandomNumber(int fd){
    short data;

    Check(read(fd, &data, sizeof(short)), "Couldn't read from stdin.\n");

    return data;
}

int GetCurrentPid(){
    return getpid();
}

int CheckIfExistsInArray(short* Numbers,int size, short num){
    for(int i = 0; i < size; i++)
    {
        if(Numbers[i] == num)
            return 1;
    }
    return 0;
}

int GetReturnValue(int duplicateCounter, const long size){
    printf("DP %d  SS %d", duplicateCounter, size);
    return (int)(((duplicateCounter/(double)size) * 100 + 5) / 10);
}

typedef struct {
    short number;
    pid_t pid;
}Record;


int main(int argc, char *argv[]) {

    if (isatty(fileno(stdin))){
//        fprintf(stderr, "stdin is a terminal\n" );
        Error("Is not a pipe\n");
    }
    else{
//        fprintf( stderr,"stdin is a file or a pipe\n");
    }
//     this sets errno so we have to reset it to 0
    errno = 0;

    if (argc < 2) {
        Error("Too few program arguments.\n");
    }

    const long ParsedArgument = ParseParam(argv[1]);
    short* Numbers = (short*)calloc(ParsedArgument*2, 1);

    int fd = STDIN_FILENO;
    Check(fd, "Couldn't open stdin");

    int duplicateCounter = 0;
    int returnValue = 0;
    size_t i;

    for (i = 0; i < ParsedArgument; i++) {
        short num = GetRandomNumber(fd);
        Numbers[i] = num;

        if(CheckIfExistsInArray(Numbers,i, num)){
            ++duplicateCounter;
//            perror("Already exists\n");
        }else {
            Record record = {Numbers[i], GetCurrentPid()};
//            fprintf(stderr, "DIGIT: %hd \tPID: %d licznik %d\n", num, GetCurrentPid(), i);
            Check(write(fileno(stdout),&record,sizeof(record)) , "Couldn't write.\n");
        }
    }

    free(Numbers);
    return GetReturnValue(duplicateCounter,ParsedArgument);
}
