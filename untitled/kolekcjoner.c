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
#include <linux/limits.h>

#define NANOSEC 1000000000L
#define FL2NANOSEC(f) {(long)(f), ((f)-(long)(f))*NANOSEC}

int DELAY = 0;
int ISNOTREADING = 0;
int NOTHINGDIED = 0;

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

typedef struct {
    short number;
    pid_t pid;
}Record;


int isRunning = 1;
static Parameters parameters = {};
int childrenCounter = 0;
pid_t* pids = NULL;
int logs;
unsigned long wiadomosci=0;
int readPipe[2];
int writePipe[2];

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

void prepareFile(int fileDescriptor)
{
    pid_t str=0;

    int recordNumber = 65535;

    for (int i = 0; i < recordNumber ; ++i) {
        write(fileDescriptor,&str,sizeof(pid_t));
    }
}

void create_child()
{
    int i = 0;
    char* poszukiwaczPath = "/home/pacto/CLionProjects/untitled/cmake-build-debug/poszukiwacz";

    while((parameters.currentVolume > 0) && (childrenCounter < parameters.maxChildren)){
        i  = FindFreePIDSlot(pids, fork());
        Check(i, "Couldn't find a free slot for child.\n");

        long childBlock = parameters.currentVolume > parameters.block ?
                          parameters.block :  parameters.currentVolume;

        if (pids[i] == -1)
        {
            Error("Failed to fork.\n");
        }else if (pids[i] > 0)
        {
            printf("child born \n");
            char logMessage[100]={};
            struct timespec start;
            clock_gettime(CLOCK_MONOTONIC, &start);
            sprintf(logMessage, "Time: %d, Info: child created. PID: %d\n",start.tv_sec , pids[i]);
            write(logs, logMessage, strlen(logMessage));
        }else {
            // we are the child
            char* logMsg = "New child born, PID: \n";
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
}

int main (int argc, char *argv[])
{

    struct timespec sleep_tm = FL2NANOSEC(2.0);


    ParseParams(argc,argv);

    logs = open(parameters.logPath, O_TRUNC | O_WRONLY, S_IRWXU);
    int table = open(parameters.successPath, O_TRUNC | O_RDWR, S_IRWXU);

    Check(logs, "Could not open logs file\n");
    Check(table, "Could not open logs file\n");
    prepareFile(table);
    pids = (pid_t*)calloc(sizeof(pid_t) * parameters.maxChildren, 1);
    fprintf(stderr,"Rodzic pid: %d\n", getpid());
    int i = 0;

    Check(pipe(writePipe), "Couldn't create a write pipe\n");
    Check(pipe(readPipe), "Couldn't create a read pipe\n");

    int flags;
    flags= fcntl(readPipe[0], F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(readPipe[0], F_SETFL, flags);

    create_child();

    int filePath = open(parameters.path,O_RDONLY );
    Check(filePath, "Couldn't open file.\n");

    char* dataReadFromFile= (char*)calloc(sizeof(char)*2*parameters.volume, 1);
//    Check(read(filePath, dataReadFromFile, sizeof(char)*2*parameters.block)), "Couldn't read from source file.\n");
    fprintf(stderr,"Data read from file %d",read(filePath, dataReadFromFile, sizeof(char)*2*parameters.volume));

    //Check(write(writePipe[1], dataReadFromFile, 2*dataRead), "Couldn't write to a pipe\n");

    Record receivedMsg = {};
    unsigned long dataWritten;
    unsigned long allData = parameters.volume*2;

    while(1)
    {
        pid_t p;
        int status;

        while ((p=waitpid(-1, &status, WNOHANG)))
        {

            if(p==-1){
                NOTHINGDIED = 1;
                break;
            }else{
                NOTHINGDIED = 0;
            }
            for (int i = 0; i < parameters.maxChildren; ++i) {
                if(pids[i] == p){
                    pids[i] = DEADCHILD;
                    if ( WIFEXITED(status) ) {
                        int es = WEXITSTATUS(status);
                        fprintf(stderr,"[rodzic] After death: %d pid: %d\n",es,p);
                        char logMessage[200];
                        struct timespec start;
                        clock_gettime(CLOCK_MONOTONIC, &start);
                        sprintf(logMessage, "Time: %d, Info: child died ;(. PID: %d job status %d\n",start.tv_sec , p, es);
                        write(logs, logMessage, strlen(logMessage));

                        --childrenCounter;
                        if(!isRunning){
                            continue;
                        }
                        create_child();
                    }
                }
            }
        }


        DELAY = ISNOTREADING && NOTHINGDIED;

        if(DELAY){
            nanosleep(&sleep_tm, NULL);
        }

        if(dataWritten < parameters.volume*2){
            long toSend = allData < PIPE_BUF ? allData : PIPE_BUF;
            int takenSizeInPipe = 0;
            ioctl(writePipe[1], FIONREAD,&takenSizeInPipe);
            if(takenSizeInPipe + toSend < 65536 - 10000){
                int w = write(writePipe[1], dataReadFromFile+dataWritten, toSend);
                if(w >= 0){
                    dataWritten += w;
                    fprintf(stderr,"\nData written %d toSend %d taken space %d\n",dataWritten,toSend, takenSizeInPipe);
                    allData -= toSend;
                }
                fprintf(stderr,"ERRNO %d\n", errno);
                Check(w, "Couldnt write t to pipe.\n");
            }

        }

        int dataRead = read(readPipe[0], &receivedMsg, sizeof(Record));

        if(dataRead == -1){
            ISNOTREADING = 1;
        }else{
            ISNOTREADING = 0;
        }

        if(childrenCounter <= 0 && dataRead <= 0)break;
        if(dataRead <= 0 ) continue;

        fprintf(stderr,"Licznik rekordow %ld Odczytano: %d tresc liczba: %hu pid: %d\n",wiadomosci,dataRead,receivedMsg.number,receivedMsg.pid);

        pid_t recordInFile;
        lseek(table, sizeof(pid_t)*receivedMsg.number, SEEK_SET);

        int countRecordInFile = read(table, &recordInFile,sizeof(pid_t));
        Check(countRecordInFile, "Couldn't read from a log file.");

        if(recordInFile==0){
            lseek(table, sizeof(pid_t)*receivedMsg.number, SEEK_SET);
            write(table, &receivedMsg.pid, sizeof(pid_t));
            wiadomosci++;
        }

        if(wiadomosci/(double)65535 > 0.75){
            isRunning = 0;
        }


        fprintf(stderr,"Zywe dzieci: %d\n",childrenCounter);
    }



    free(dataReadFromFile);
    close(logs);
    close(writePipe[0]);
    close(writePipe[1]);
    close(readPipe[0]);
    close(readPipe[1]);
    return 0;
}

