#ifndef _LF_THPOOL_H
#define _LF_THPOOL_H

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define THREAD_COUNT 4
#define WORK_QUEUE_SIZE 65535

typedef struct {
    void (*function)(void *);
    void *arg;
} task_t;

/* define a thread structure */
typedef struct {
    // task_t *buffer;
    task_t buffer[WORK_QUEUE_SIZE];  // workqueue
    int id;                          // thread id
    pthread_t thr;                   // the actual thread
    int size;                        // size of queue
    unsigned int in;                 // index of pushing a task
    unsigned int out;                // index of popping a task
    volatile unsigned task_count;    // number of tasks in queue
} thread_t;

/* define a thread pool structure */
typedef struct {
    // thread_t *threads;
    int thread_count;
    int is_stopped;
    thread_t threads[];
} lf_thpool_t;

/* utils */
lf_thpool_t *lf_thpool_create(int thread_count, int queue_size);
int lf_thpool_destroy(lf_thpool_t *lf_thpool);
task_t *lf_thpool_deq(thread_t *thread);
void lf_thpool_enq(lf_thpool_t *lf_thpool, void (*task)(void *), void *arg);
thread_t *round_robin_schedule(lf_thpool_t *lf_thpool);
int dispatch_task(thread_t *thread, void (*task)(void *), void *arg);

#endif