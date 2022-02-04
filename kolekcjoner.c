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
#include <sys/stat.h>

typedef struct {
    char* path; // sciezka do pliki z danymi do pobrania
    long volume; // liczba danych do pobrania przez wzystkie podprocesy
    long block; // liczba danych do pobrania przez jeden proces
    char* successPath; // sciezka na odnotowanie osiagniec
    char* logPath; // sciezka na logi
    int maxChildren; // maksymalna liczba potomkow
} Parameters;

static Parameters parameters = {};

void Error(const char* errormsg){
    perror(errormsg);
    exit(EXIT_FAILURE);
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

void Check(int value, const char * errormsg){
    if (value == -1)
        Error(errormsg);
}

void ParseParams(int argc, char * argv[]){
    if (argv == NULL){
        fprintf(stderr, "ParseParameters(): argv is NULL\n");
        exit(1);
    }
    int opt;
    char *ptr;
    long ret;
    while ((opt = getopt(argc, argv, "d:s:w:f:l:p:")) != -1) {
        switch (opt) {
            case 'd': //sciezka do danych
                parameters.path = optarg;
                printf("parameters.path %s\n", parameters.path);
                break;
            case 's': //Ilosc danych jakie chcemmy zassac
                parameters.volume = ParseParam(optarg);
                printf("parameters.volume %ld\n", parameters.volume);
                break;
            case 'w': //Ilosc danych do zassania przez potomka
                parameters.block = ParseParam(optarg);
                printf("parameters.block %ld\n", parameters.block);
                break;
            case 'f': // Sciezka do pliku z osiagnieciami
                parameters.successPath = optarg;
                printf("parameters.successPath %s\n", parameters.successPath);
                break;
            case 'l':// sciezka do raportow o potomkack
                parameters.logPath = optarg;
                printf("parameters.logPath %s\n", parameters.logPath);
                break;
            case 'p': // maksymalne ilosc potomkow
                ret = strtol(optarg, &ptr, 10);
                parameters.maxChildren = ret;
                printf("parameters.maxChildren %d\n", parameters.maxChildren);
                break;
            default:
                Error("Error parsing arguments.\n");
        }
    }
}

int main (int argc, char *argv[])
{

    ParseParams(argc,argv);
    char* poszukiwaczPath = "poszukiwacz";
    int i = 0;
    pid_t* pids = (pid_t*)calloc(sizeof(pid_t) * parameters.maxChildren, 1);

    struct stat finfo;
    int childrenCounter = 0;

    int readPipe[2];
    int writePipe[2];



    Check(pipe(readPipe), "Couldn't create a read pipe\n");
    Check(pipe(writePipe), "Couldn't create a write pipe\n");

    if (isatty(fileno(stdin)))
        printf( "stdin is a terminal\n" );
    else
        printf( "stdin is a file or a pipe\n");


    while(childrenCounter < 5){
        pids[i] = fork();

        printf("NUM VOLUME %ld.\n", parameters.volume - parameters.block);
        if (pids[i] == -1)
        {
            Error("Failed to fork.\n");
        }else if (pids[i] > 0)
        {
            int status;
            printf("child born \n");
        }else {
            // we are the child
            char buff[5000];
            sprintf(buff, "%ld", parameters.block);
            char * args[3] = {"/home/pacto/CLionProjects/untitled1/cmake-build-debug/poszukiwacz", buff, NULL};

            printf("We are child \n");
//            close(readPipe[1]);
            Check(dup2(readPipe[0], fileno(stdin)), "Couldn't dup2 for readPipe\n");
            close(readPipe[0]);

//            close(writePipe[0]);
            Check(dup2(writePipe[1], fileno(stdout)), "Couldn't dup2 for writePipe\n");
            close(writePipe[1]);

            Check(execv("/home/pacto/CLionProjects/untitled1/cmake-build-debug/poszukiwacz", args), "Exec.\n");
        }
        long childBlock = parameters.volume - parameters.block > parameters.block ? parameters.block :  parameters.volume;
        parameters.volume -= parameters.block;
        ++childrenCounter;
        ++i;
    }

    close(readPipe[1]);
    close(writePipe[0]);

    int filePath = open("/dev/urandom",O_RDONLY );

    Check(filePath, "Couldn't open file.\n");

    const int dataRead = 1024;
    char* dataReadFromFile= (char*)calloc(sizeof(char)*dataRead*2, 1);
    Check(read(filePath, dataReadFromFile, 2*dataRead), "Couldn't read from source file.\n");

    printf("Data read %d\n", *dataReadFromFile);


    Check(filePath, "Couldn't open file to read from.\n");
    Check(write(writePipe[1], dataReadFromFile, 2*dataRead), "Couldn't write to a pipe\n");


    char dataReadFromPipe[dataRead];
    Check(read(readPipe[0], dataReadFromPipe,dataRead), "Couldn't write to a pipe\n");

    printf("Data read from pipe %d\n", *dataReadFromPipe);

    return 0;
}


