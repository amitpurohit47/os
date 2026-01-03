    /*
Goals:
1. Create a Lock Free Queue 
2. Multiple Producers will produce data
3. Multiple Consumers will consume the data and process it
4. Consumers won't directly log the data
5. Instead will use Log Workers to log data

Considerations:
1. Use Shared memory
2. Handle Race conditions
3. Avoid use of mutex
4. Workers don't engage in I/O

Design
1. We need to fork processes for Producers, Workers, Loggers
2. Simulate producers producing data via loops
3. They write data to shared buffer
4. Buffer has a fixed size and is circular in nature
5. We communicate with workers via semaphores
6. Workers access buffer based on semaphore contract
7. Workers write data to logger buffer

Race Conditions
1. Multiple producers having access to task queue, but differ in stages
2. Handle such writes to unready buffer slots to which producers are still writing but consumers have started consuming

Entities
1. Task Buffer Entity - Producer id, data, ready
2. Log Buffer Entity - Worker id, data, ready
3. Task Buffer - Task Entity array, in and out pointers, full and empty semaphores
4. Log Buffer - Log Entity array, in and out pointers, full and empty semaphores
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/mman.h>

#define ull unsigned long long
#define BUFFER_SIZE 1024
#define MIN_TASK_SIZE 10000000
#define MAX_TASK_SIZE 100000000
#define WORKERS 10
#define PRODUCERS 5
#define LOGGERS 2

typedef struct {
    pid_t producerPid;
    int data;
    // using volatile to inform cpu that value might change any time so fetch from memory always
    volatile int ready;
} TaskEntity;

typedef struct {
    pid_t workerPid;
    pid_t  producerPid;
    int data;
    volatile int ready;
} LogEntity;

typedef struct {
    TaskEntity buffer[BUFFER_SIZE];
    ull in;
    ull out;
    sem_t full;
    sem_t empty;
} TaskBuffer;

typedef struct {
    LogEntity buffer[BUFFER_SIZE];
    ull in;
    ull out;
    sem_t full;
    sem_t empty;
} LogBuffer;

void producer(TaskBuffer *tb) {
    // seed the random number generator to produce true random numbers
    // instead of same random numbers every time
    srand(time(NULL));

    ull taskSize = MIN_TASK_SIZE + rand() % MAX_TASK_SIZE;

    for (ull i = 0; i < taskSize; i++) {
        // wait if we have no empty slots in buffer
        sem_wait(&tb->empty);
        // __sync_fetch_and_add is an important function here
        // tb->in++ is a 3 phase operation
        // load, add and store
        // this function translates to single cpu instruction 
        // the instruction is lock add
        // it is atomic
        // it prevents context switching in the middle of the update
        // updates the value and return previous value
        ull prevIn = __sync_fetch_and_add(&tb->in, 1);
        tb->buffer[prevIn % BUFFER_SIZE].producerPid = getpid();
        tb->buffer[prevIn % BUFFER_SIZE].data = rand() % INT_MAX;

        // __sync_synchronize acts as a memory barrier
        // CPU and compilers tend to shuffle instructions for optimal performance
        // this method instructs compiler and cpu to perform all memory opertions before this call
        __sync_synchronize();

        // indicate that new data is ready at the slot so that worker can pick this up if it is waiting
        tb->buffer[prevIn % BUFFER_SIZE].ready = 1;

        // post that the buffer slot is full
        sem_post(&tb->full);
    }
}

void worker(TaskBuffer *tb, LogBuffer *lb) {
    while (1) {
        int data;
        pid_t workerPid, producerPid;

        // worker is sleeping until there is no free slot
        sem_wait(&tb->full);
        ull prevOut = __sync_fetch_and_add(&tb->out, 1);

        // slot at prevOut might contain garbage data
        // wait until the data at the location is ready while the producer is writing to the location
        while (tb->buffer[prevOut % BUFFER_SIZE].ready == 0) {
            // sleep only for 1 microsecond since writes can happen quickly
            usleep(1);
        }
        data = tb->buffer[prevOut % BUFFER_SIZE].data;
        workerPid = getpid();
        producerPid = tb->buffer[prevOut % BUFFER_SIZE].producerPid;
        __sync_synchronize();
        tb->buffer[prevOut % BUFFER_SIZE].ready = 0;
        sem_post(&tb->empty);

        if (data == -1) {
            exit(0);
        }

        // write to log buffer
        // this ensures worker is not involved in I/O operations

        sem_wait(&lb->empty);
        ull prevIn = __sync_fetch_and_add(&lb->in, 1);
        lb->buffer[prevIn % BUFFER_SIZE].data = data;
        lb->buffer[prevIn % BUFFER_SIZE].producerPid = producerPid;
        lb->buffer[prevIn % BUFFER_SIZE].workerPid = workerPid;
        __sync_synchronize();
        lb->buffer[prevIn % BUFFER_SIZE].ready = 1;
        sem_post(&lb->full);
    }
}

void logger(LogBuffer *lb) {
    while (1) {
        int data;
        pid_t workerPid, producerPid;
        sem_wait(&lb->full);
        ull prevOut = __sync_fetch_and_add(&lb->out, 1);
        while (lb->buffer[prevOut % BUFFER_SIZE].ready == 0) {
            usleep(1);
        }
        data = lb->buffer[prevOut % BUFFER_SIZE].data;
        workerPid = lb->buffer[prevOut % BUFFER_SIZE].workerPid;
        producerPid = lb->buffer[prevOut % BUFFER_SIZE].producerPid;
        __sync_synchronize();
        lb->buffer[prevOut % BUFFER_SIZE].ready = 0;
        sem_post(&lb->empty);
        if (data == -1) {
            exit(0);
        }
        printf("[LOG] Worker %d processed %d produced by %d\n", workerPid, data, producerPid);
    }
}


int main() {
    // Create shared memory for task buffer and log buffer
    // NUll indicates starting address to be confiured by OS itself
    // PROT_READ | PROT_WRITE indicates permissions over shared memory
    // MAP_SHARED | MAP_ANONYMOUS indicates memory is shared and does not have underlying file binding
    TaskBuffer *tb = mmap(NULL, sizeof(TaskBuffer), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    LogBuffer *lb = mmap(NULL, sizeof(LogBuffer), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    // Semaphore initialization
    sem_init(&tb->empty, 1, BUFFER_SIZE);
    sem_init(&tb->full, 1, 0);
    sem_init(&lb->empty, 1, BUFFER_SIZE);
    sem_init(&lb->full, 1, 0);

    // hold process ids
    pid_t producers[PRODUCERS];
    pid_t workers[WORKERS];
    pid_t loggers[LOGGERS];

    // spawn producers
    for (int i = 0; i < PRODUCERS; i++) {
        if((producers[i] = fork()) == 0) {
            producer(tb);
            exit(0);
        }
    }

    // spwan workers
    for (int i = 0; i < WORKERS; i++) {
        if((workers[i] = fork()) == 0) {
            worker(tb, lb);
            exit(0);
        }
    }

    // spwan loggers
    for (int i = 0; i < LOGGERS; i++) {
        if((loggers[i] = fork()) == 0) {
            logger(lb);
            exit(0);
        }
    }

    // wait for producers to finish
    // producers must finish producing before finishing the consumers
    for (int i = 0; i < PRODUCERS; i++) {
        waitpid(producers[i], NULL, 0);
    }

    // send poison pill to worker
    for (int i = 0; i < WORKERS; i++) {
        sem_wait(&tb->empty);
        ull prevIn = __sync_fetch_and_add(&tb->in, 1);
        tb->buffer[prevIn % BUFFER_SIZE].data = -1;
        tb->buffer[prevIn % BUFFER_SIZE].producerPid = getpid();
        __sync_synchronize();
        tb->buffer[prevIn % BUFFER_SIZE].ready = 1;
        sem_post(&tb->full);
    }

    // send poison pill to loggers
    for (int i = 0; i < LOGGERS; i++) {
        sem_wait(&lb->empty);
        ull prevIn = __sync_fetch_and_add(&lb->in, 1);
        lb->buffer[prevIn % BUFFER_SIZE].data = -1;
        lb->buffer[prevIn % BUFFER_SIZE].producerPid = getpid();
        __sync_synchronize();
        lb->buffer[prevIn % BUFFER_SIZE].ready = 1;
        sem_post(&lb->full);
    }

    // wait for workers to finish
    for (int i = 0; i < WORKERS; i++) {
        waitpid(workers[i], NULL, 0);
    }

    // wait for loggers to finish
    for (int i = 0; i < LOGGERS; i++) {
        waitpid(loggers[i], NULL, 0);
    }

    // free up shared memory after completing the process
    munmap(tb, sizeof(TaskBuffer));
    munmap(lb, sizeof(LogBuffer));
    return 0;
}

