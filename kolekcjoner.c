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

#define DEADCHILD -10
typedef struct {
    char* path; // sciezka do pliki z danymi do pobrania
    long volume; // liczba danych do pobrania przez wzystkie podprocesy
    long currentVolume; // liczba danych do pobrania przez wzystkie podprocesy w danym momencie
    long block; // liczba danych do pobrania przez jeden proces
    char* successPath; // sciezka na odnotowanie osiagniec
    char* logPath; // sciezka na logi
    int maxChildren; // maksymalna liczba potomkow
} Parameters;

static Parameters parameters = {};
int childrenCounter = 0;
pid_t* pids = NULL;

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
                break;
            case 's': //Ilosc danych jakie chcemmy zassac
                parameters.volume = ParseParam(optarg);
                parameters.currentVolume = ParseParam(optarg);
                break;
            case 'w': //Ilosc danych do zassania przez potomka
                parameters.block = ParseParam(optarg);
                break;
            case 'f': // Sciezka do pliku z osiagnieciami
                parameters.successPath = optarg;
                break;
            case 'l':// sciezka do raportow o potomkack
                parameters.logPath = optarg;
                break;
            case 'p': // maksymalne ilosc potomkow
                ret = strtol(optarg, &ptr, 10);
                parameters.maxChildren = ret;
                break;
            default:
                Error("Error parsing arguments.\n");
        }
    }
}
int FindFreePIDSlot(pid_t* passedpids, pid_t pid){
    for (int i = 0; i < parameters.maxChildren; ++i) {
        if(passedpids[i] == DEADCHILD || passedpids[i] == 0){
            passedpids[i] = pid;
            ++childrenCounter;
            return i;
        }
    }
    return -1;
}
void HandleChildDeath(int signal){
    pid_t p;
    int status;

    while ((p=waitpid(-1, &status, WNOHANG)) != -1)
    {
        for (int i = 0; i < parameters.maxChildren; ++i) {
            if(pids[i] == p){
                pids[i] = DEADCHILD;
                --childrenCounter;
            }
        }
    }
}

int main (int argc, char *argv[])
{
    ParseParams(argc,argv);
    pids = (pid_t*)calloc(sizeof(pid_t) * parameters.maxChildren, 1);
    sigaction(SIGCHLD, HandleChildDeath, NULL);

    int i = 0;
    char* poszukiwaczPath = "/home/pacto/CLionProjects/untitled/cmake-build-debug/poszukiwacz";

    int readPipe[2];
    int writePipe[2];

    Check(pipe(writePipe), "Couldn't create a write pipe\n");
    Check(pipe(readPipe), "Couldn't create a read pipe\n");

    while((parameters.currentVolume > 0) && (childrenCounter < parameters.maxChildren)){
        i  = FindFreePIDSlot(pids, fork());
        Check(i, "Couldn't find a free slot for child.\n");

        long childBlock = parameters.currentVolume - parameters.block > parameters.block ?
                parameters.block :  parameters.currentVolume;

        if (pids[i] == -1)
        {
            Error("Failed to fork.\n");
        }else if (pids[i] > 0)
        {
            printf("child born \n");
        }else {
            // we are the child
            char buff[5000];
            sprintf(buff, "%ld", childBlock);
            char * args[3] = {poszukiwaczPath, buff, NULL};

            Check(dup2(readPipe[1], STDOUT_FILENO), "Couldn't dup2 for readPipe\n");
            close(readPipe[0]);
            close(readPipe[1]);

            Check(dup2(writePipe[0], STDIN_FILENO), "Couldn't dup2 for writePipe\n");
            close(writePipe[0]);
            close(writePipe[1]);

            Check(execv(poszukiwaczPath, args), "Exec.\n");
        }
        parameters.currentVolume -= childBlock;
    }

    close(writePipe[0]);
    close(readPipe[1]);

    int filePath = open(parameters.path,O_RDONLY );
    Check(filePath, "Couldn't open file.\n");

    char* dataReadFromFile= (char*)calloc(sizeof(char)*2*parameters.block, 1);
//    Check(read(filePath, dataReadFromFile, sizeof(char)*2*parameters.block)), "Couldn't read from source file.\n");
    fprintf(stderr,"Data read from file %d",read(filePath, dataReadFromFile, sizeof(char)*2*parameters.block));

    //Check(write(writePipe[1], dataReadFromFile, 2*dataRead), "Couldn't write to a pipe\n");
    fprintf(stderr,"\n%d\n",write(writePipe[1], dataReadFromFile, sizeof(char)*2*parameters.block), "Couldn't write to a pipe\n");

    char* dataReadFromChild = (char*)calloc(sizeof(char)*2*parameters.block, 1);
    for (int j = 0; j < 10; ++j) {
        fprintf(stderr,"Read from a child %d\n",read(readPipe[0], dataReadFromChild, 2), "Couldn't read from a pipe\n");
        fprintf(stderr,"Read from a child %d\n", *dataReadFromChild);
    }

    for (int j = 0; j < parameters.maxChildren; ++j) {
        waitpid(pids[i], NULL, 0);
    }

    return 0;
}


