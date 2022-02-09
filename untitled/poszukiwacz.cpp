#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#define EXIT_FAIL 11
//#define DEBUG_ON
// debug which takes any arguments which then get passed to fprintf
#ifdef DEBUG_ON
#define Debug(x, ...) fprintf(stderr, x, __VA_ARGS__)
#else
#define Debug(x, ...)
#endif

typedef struct {
    uint16_t number;
    pid_t pid;
} Record;

typedef struct {
    unsigned long dataToRead;
    int duplicateCounter;
    uint16_t *allNumbers;
} ProgramData;

ProgramData programData;

void Error(const char *errormsg);
void Check(int value, const char *errormsg);
void CheckIfPipe();
void CheckProgramArgumentSize(int argc, char *argv[]);

long ParseUnit(const char *unit);
long ParseParam(const char *param);

uint16_t GetNumberFromInput(int fd);

int CheckIfExistsInArray(const uint16_t *Numbers, int size, short num);
void InitProgramData(const char *param);
int GetReturnValue();

void QueryData();

int main(int argc, char *argv[]) {
    CheckIfPipe();
    CheckProgramArgumentSize(argc, argv);

    InitProgramData(argv[1]);
    QueryData();

    free(programData.allNumbers);
    int returnValue = GetReturnValue();

    Debug("Return value %d.\n", returnValue);
    return returnValue;
}


void Error(const char *errormsg) {
    perror(errormsg);
    exit(EXIT_FAIL);
}
void Check(int value, const char *errormsg) {
    if (value == -1)
        Error(errormsg);
}

void CheckIfPipe() {
    if (isatty(fileno(stdin))) {
        Error("Is not a pipe\n");
    }
    // this sets errno so we have to reset it to 0
    errno = 0;
}

void CheckProgramArgumentSize(int argc, char *argv[]) {
    if (argv == NULL) {
        Error("argv is NULL\n");
    }
    if (argc < 2) {
        Error("Too few program arguments.\n");
    }
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
    if(result < 0){
        Error("Argument is not a positive number.\n");
    }
    return  result;
}


uint16_t GetNumberFromInput(int fd) {
    uint16_t data;
    Check(read(fd, &data, sizeof(uint16_t)), "Couldn't read from stdin.\n");

    Debug("Data read from stdin: Number %hu.\n", data);
    return data;
}

int CheckIfExistsInArray(const uint16_t *Numbers, int size, short num) {
    for (int i = 0; i < size; i++) {
        if (Numbers[i] == num)
            return 1;
    }
    return 0;
}

void InitProgramData(const char *param) {
    const unsigned long dataToRead = ParseParam(param);
    uint16_t* allNumbers = (uint16_t *) calloc(dataToRead * 2, 1);
    if(allNumbers == NULL){
        Error("Couldn't allocate allNumbers array.\n");
    }

    programData = {dataToRead, 0, allNumbers};
}

int GetReturnValue() {
    return (int) (((programData.duplicateCounter / (double) programData.dataToRead) * 100 + 5) / 10);
}

void QueryData() {
    int fd = STDIN_FILENO;

    for (size_t i = 0; i < programData.dataToRead; i++) {
        uint16_t num = GetNumberFromInput(fd);
        programData.allNumbers[i] = num;

        if (CheckIfExistsInArray(programData.allNumbers, i, num)) {
            ++programData.duplicateCounter;
        } else {
            Record record = {programData.allNumbers[i], getpid()};
            Check(write(STDOUT_FILENO, &record, sizeof(record)), "Couldn't write.\n");
            Debug("Data written to stdout: Number %hu\tPID %d.\n", record.number, record.pid);
        }
    }
}
