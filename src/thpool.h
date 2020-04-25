#ifndef _THPOOL_H
#define _THPOOL_H

#include <fcntl.h>
#include <pthread.h>
// #include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


/* define a task node */
typedef struct {
    void (*function)(void *);
    void *arg;
} task_t;

/* define a task queue */
typedef struct {
    task_t *buffer;  // ring-buffer task queue
    int size;        // size of queue
    int in;          // index of pushing a task
    int out;         // index of popping a task
    int task_count;  // number of tasks in queue
} taskqueue_t;

/* define a thread pool */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t *threads;
    taskqueue_t *queue;
    int thread_count;
    int is_stopped;
} thpool_t;

thpool_t *thpool_create(int thread_count, int queue_size);
int thpool_destroy(thpool_t *thpool);
task_t *thpool_deq(thpool_t *thpool);
void thpool_enq(thpool_t *thpool, void (*task)(void *), void *arg);
int thpool_q_empty(thpool_t *thpool);

#endif