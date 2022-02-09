#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

//#define DEBUG_ON
// debug which takes any arguments which then get passed to fprintf
#ifdef DEBUG_ON
#define Debug(x, ...) fprintf(stderr, x, __VA_ARGS__)
#else
#define Debug(x, ...)
#endif

#define POSZUKIWACZ_PATH "./poszukiwacz"
#define RECORDS 65536   // max number of records in file - max amount of values which can be stored on 2 bytes
#define DEADCHILD -10

// taken from class notes
#define NANOSEC 1000000000L
#define FL2NANOSEC(f) \
    { (long) (f), ((f) - (long) (f)) * NANOSEC }

int IS_NOT_READING = 0;
int NOTHING_DIED = 0;
int createMoreChildren = 1;

typedef struct {
    char *path;        // path to source file
    long volume;       // amount of data to read total
    long currentVolume;// data left to be distributed
    long block;        // amount of data to be read by a single "poszukiwacz"
    char *successPath; // path to log successful finds
    char *logPath;     // path to log logs
    int maxChildren;   // maximum number of "poszukiwacz" processes at once
} Parameters;

typedef struct {
    uint16_t number;
    pid_t pid;
} Record;

typedef struct {
    int readPipe[2];
    int writePipe[2];

    pid_t *pids;
} ProgramData;

typedef struct {
    int logs;
    int successes;
    int sourceFile;
} ProgramFiles;

typedef struct {
    unsigned long messageCounter;
    unsigned long childCounter;
    unsigned long dataReceived;
    unsigned long recordCounter;
} CommunicationData;

// Creating only one instance globally not to keep passing
// values underneath as arguments constantly
Parameters parameters = {};
ProgramData programData = {};
ProgramFiles programFiles = {};
CommunicationData communicationData = {};

void Error(const char *errormsg);
void Check(int value, const char *errormsg);

long ParseUnit(const char *unit);
long ParseParam(const char *param);

void ParseParams(int argc, char *argv[]);
int FindFreePIDSlot(pid_t *pids, pid_t pid);

void FillSuccessesFile(int fileDescriptor);

void HandleFiles();
void HandlePipes();
void CloseAll();

void InitProgram(int argc, char *argv[]);

void CreateChildren();
void ConditionalDelay(int condition, double time);

void LogDeath(int deathStatus, pid_t p);
void LogBirth(pid_t pid);

void WriteToSuccessFile(Record receivedMessage);
void CheckFileOverflow();// Checks if 75% of records in file are filled, if yes it flips a flag to stop creating children
void CheckChildStatus();

int main(int argc, char *argv[]) {
    InitProgram(argc, argv);

    CreateChildren();

    char *dataReadFromFile = (char *) calloc(sizeof(uint16_t) * parameters.volume, 1);
    int bytesReadFromFile = read(programFiles.sourceFile, dataReadFromFile, sizeof(uint16_t) * parameters.volume);

    Check(bytesReadFromFile, "Couldn't read from source file.\n");
    Debug("Bytes read from source file %d.\n", bytesReadFromFile);

    Record receivedMessage = {};
    unsigned long dataWritten = 0;
    unsigned long allData = parameters.volume * 2;

    while (1) {
        ConditionalDelay(IS_NOT_READING & NOTHING_DIED, 0.48);

        CheckChildStatus();

        if (dataWritten < parameters.volume * 2) {
            // Check free space in pipe buff
            long toSend = allData < PIPE_BUF ? allData : PIPE_BUF;
            int takenSizeInPipe = 0;
            Check(ioctl(programData.writePipe[1], FIONREAD, &takenSizeInPipe), "Couldn't check pipe size.\n");
            // Subtract from the pipe's max size to avoid overflow problems
            if (takenSizeInPipe + toSend < 65536 - 10000) {
                // Only send PIPE_BUF or less data to keep data atomic
                int w = write(programData.writePipe[1], dataReadFromFile + dataWritten, toSend);
                if (w >= 0) {
                    dataWritten += w;
                    allData -= w;
                }
                Check(w, "Couldn't write to a pipe.\n");
            }
        }

        int dataRead = read(programData.readPipe[0], &receivedMessage, sizeof(Record));

        if (dataRead == -1) {
            IS_NOT_READING = 1;
        } else {
            communicationData.dataReceived += dataRead;
            IS_NOT_READING = 0;
        }

        if (communicationData.childCounter <= 0 && dataRead <= 0) break;
        if (dataRead <= 0) continue;

        Debug("Records read %ld\tData read: %d\tReceived data: Number %hu, PID %d.\n", communicationData.recordCounter, dataRead, receivedMessage.number, receivedMessage.pid);

        WriteToSuccessFile(receivedMessage);

        CheckFileOverflow();

        Debug("Currently have %lu running processes.\n", communicationData.childCounter);
    }

    Debug("Total data received %lu.\n", communicationData.dataReceived);
    free(dataReadFromFile);
    CloseAll();
    return EXIT_SUCCESS;
}

long ParseUnit(const char *unit) {
    if (strcmp(unit, "Ki") == 0) {
        return 1024;
    } else if (strcmp(unit, "Mi") == 0) {
        return 1024 * 1024;
    } else if (strcmp(unit, "") == 0) {
        return 1;
    }
    return -1;
}

long ParseParam(const char *param) {
    char *ptr;

    long ret = strtol(param, &ptr, 10);
    long unit = ParseUnit(ptr);
    Check(unit, "Passed in wrong unit to the arguments.\n");

    long result = ret * unit;
    if (result < 0) {
        Error("Argument is not a positive number.\n");
    }
    return result;
}

void Error(const char *errormsg) {
    perror(errormsg);
    exit(EXIT_FAILURE);
}

void Check(int value, const char *errormsg) {
    if (value == -1)
        Error(errormsg);
}

void ParseParams(int argc, char *argv[]) {
    if (argv == NULL) {
        Error("argv is NULL.\n");
    }
    if (argc != 13) {
        Error("Wrong number of arguments passed.\n"
              "\nUSAGE:\n"
              "-d\tpath to source file to read from.\n"
              "-s\ttotal amount of data to read. Can use \"Ki\" or \"Mi\" units.\n"
              "-w\tdata to be read by a single child.\n"
              "-f\tpath to file to store successes in.\n"
              "-l\tpath to file to store logs in.\n"
              "-p\tamount of children allowed to be alive at one time.\n\n"
              );
    }

    int opt;
    char *ptr;
    long ret;
    while ((opt = getopt(argc, argv, "d:s:w:f:l:p:")) != -1) {
        switch (opt) {
            case 'd':
                parameters.path = optarg;
                break;
            case 's':
                parameters.volume = ParseParam(optarg);
                parameters.currentVolume = parameters.volume;
                break;
            case 'w':
                parameters.block = ParseParam(optarg);
                break;
            case 'f':
                parameters.successPath = optarg;
                break;
            case 'l':
                parameters.logPath = optarg;
                break;
            case 'p':
                ret = strtol(optarg, &ptr, 10);
                parameters.maxChildren = ret;
                break;
            default:
                Error("Error parsing arguments.\n");
        }
    }
}

int FindFreePIDSlot(pid_t *pids, pid_t pid) {
    for (int i = 0; i < parameters.maxChildren; ++i) {
        if (pids[i] == DEADCHILD || pids[i] == 0) {
            pids[i] = pid;
            ++communicationData.childCounter;
            return i;
        }
    }
    return -1;
}
void CreateChildren() {
    while ((parameters.currentVolume > 0) && (communicationData.childCounter < parameters.maxChildren)) {
        int i = FindFreePIDSlot(programData.pids, fork());
        Check(i, "Couldn't find a free slot for a child.\n");

        long childBlock = parameters.currentVolume > parameters.block ? parameters.block : parameters.currentVolume;

        Check(programData.pids[i], "Couldn't create fork.\n");

        if (programData.pids[i] > 0) {
            Debug("Child born with PID: %d\n", programData.pids[i]);
            LogBirth(programData.pids[i]);
        } else {
            char buff[32];
            if (sprintf(buff, "%ld", childBlock) < 0) {
                Error("Couldn't format string with sprintf.\n");
            }
            char *args[3] = {POSZUKIWACZ_PATH, buff, NULL};

            Check(dup2(programData.readPipe[1], STDOUT_FILENO), "Couldn't dup2 for readPipe\n");
            close(programData.readPipe[0]);
            close(programData.readPipe[1]);

            Check(dup2(programData.writePipe[0], STDIN_FILENO), "Couldn't dup2 for writePipe\n");
            close(programData.writePipe[0]);
            close(programData.writePipe[1]);

            Check(execv(POSZUKIWACZ_PATH, args), "Error when doing exec.\n");
        }
        parameters.currentVolume -= childBlock;
    }
}

void FillSuccessesFile(int fileDescriptor) {
    Check(ftruncate(fileDescriptor, sizeof(pid_t)*RECORDS),"Couldn't fill the success file.\n");
}
void HandlePipes() {
    programData.pids = (pid_t *) calloc(sizeof(pid_t) * parameters.maxChildren, 1);
    if (programData.pids == NULL) {
        Error("Couldn't allocate pids array.\n");
    }

    Check(pipe(programData.writePipe), "Couldn't create a write pipe\n");
    Check(pipe(programData.readPipe), "Couldn't create a read pipe\n");

    // taken from The Linux Programming Interface
    int flags = fcntl(programData.readPipe[0], F_GETFL);
    flags |= O_NONBLOCK;
    Check(fcntl(programData.readPipe[0], F_SETFL, flags), "Couldn't set readPipe[0] to nonblocking.\n");
}

void HandleFiles() {
    programFiles.logs = open(parameters.logPath, O_CREAT | O_TRUNC | O_WRONLY, S_IWRITE | S_IREAD);
    programFiles.successes = open(parameters.successPath, O_CREAT | O_TRUNC | O_RDWR, S_IWRITE | S_IREAD);
    programFiles.sourceFile = open(parameters.path, O_RDONLY);

    Check(programFiles.logs, "Could not open/create logs file\n");
    Check(programFiles.successes, "Could not open/create successes file\n");
    Check(programFiles.sourceFile, "Couldn't open source file.\n");

    FillSuccessesFile(programFiles.successes);
}
void InitProgram(int argc, char *argv[]) {
    ParseParams(argc, argv);

    HandleFiles();
    HandlePipes();
}
void WriteToSuccessFile(Record receivedMessage) {
    pid_t recordInFile;
    Check(lseek(programFiles.successes, sizeof(pid_t) * receivedMessage.number, SEEK_SET), "Couldn't lseek in success file.\n");

    int countRecordInFile = read(programFiles.successes, &recordInFile, sizeof(pid_t));
    Check(countRecordInFile, "Couldn't read from a log file.");

    if (recordInFile == 0) {
        Check(lseek(programFiles.successes, sizeof(pid_t) * receivedMessage.number, SEEK_SET), "Couldn't lseek in success file.\n");
        Check(write(programFiles.successes, &receivedMessage.pid, sizeof(pid_t)), "Couldn't write to success file.\n");
        communicationData.recordCounter++;
    }
}

void ConditionalDelay(int condition, double timeInSec) {
    if (condition) {
        struct timespec sleep_tm = FL2NANOSEC(timeInSec);
        Check(nanosleep(&sleep_tm, NULL), "Couldn't nanosleep.\n");
    }
}

void CloseAll() {
    close(programFiles.logs);
    close(programFiles.successes);
    close(programFiles.sourceFile);
    close(programData.writePipe[0]);
    close(programData.writePipe[1]);
    close(programData.readPipe[0]);
    close(programData.readPipe[1]);
}

void LogDeath(int deathStatus, pid_t p) {
    char logMessage[200];
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (sprintf(logMessage, "[Time: %ld] Child died. PID: %d, Job status: %d\n", start.tv_sec, p, deathStatus) < 0) {
        Error("Couldn't format string with sprintf.\n");
    }
    Check(write(programFiles.logs, logMessage, strlen(logMessage)), "Couldn't write to log file.\n");
}
void LogBirth(pid_t pid) {
    char logMessage[100] = {};
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (sprintf(logMessage, "[Time: %ld] Child born. PID: %d\n", start.tv_sec, pid) < 0) {
        Error("Couldn't format string with sprintf.\n");
    }
    Check(write(programFiles.logs, logMessage, strlen(logMessage)), "Couldn't write to a log file.\n");
}

void CheckFileOverflow() {
    if ((communicationData.recordCounter / (double) 65535) > 0.75) {
        createMoreChildren = 0;
    }
}
void CheckChildStatus() {
    pid_t p = 0;
    int status = 0;

    while ((p = waitpid(-1, &status, WNOHANG))) {

        if (p == -1) {
            NOTHING_DIED = 1;
            break;
        } else {
            NOTHING_DIED = 0;
        }
        for (int i = 0; i < parameters.maxChildren; ++i) {
            if (programData.pids[i] == p) {
                programData.pids[i] = DEADCHILD;
                if (WIFEXITED(status)) {
                    int deathStatus = WEXITSTATUS(status);
                    Debug("Child with PID: %d just died with status %d.\n", p, deathStatus);
                    if (deathStatus > 10) {
                        Error("Child returned with error status (>10).\n");
                    }
                    LogDeath(deathStatus, p);

                    --communicationData.childCounter;

                    if (!createMoreChildren) continue;

                    CreateChildren();
                }
            }
        }
    }
}
