#include "thpool.h"


static void perform_tasks();
static void *worker_thread_cycle(void *arg);
static void thread_sleep(thpool_t *thpool);
static void thread_wakeup(thpool_t *thpool);

thpool_t *thpool_create(int thread_count, int queue_size)
{
    /* TODO: add error handler for initialization */
    thpool_t *thpool;
    thpool = (thpool_t *) malloc(sizeof(thpool_t));
    pthread_mutex_init(&(thpool->lock), NULL);
    pthread_cond_init(&(thpool->cond), NULL);
    thpool->thread_count = thread_count;
    thpool->is_stopped = 0;

    thpool->threads = (pthread_t *) malloc(sizeof(pthread_t) * thread_count);

    thpool->queue = (taskqueue_t *) malloc(sizeof(taskqueue_t));
    thpool->queue->in = 0;
    thpool->queue->out = 0;
    thpool->queue->size = queue_size;
    thpool->queue->task_count = 0;

    thpool->queue->buffer = (task_t *) malloc(sizeof(task_t) * queue_size);

    for (int i = 0; i < thread_count; ++i) {
        pthread_create(thpool->threads + i, NULL, worker_thread_cycle,
                       (void *) thpool);
    }

    return thpool;
}

int thpool_destroy(thpool_t *thpool)
{
    for (int i = 0; i < thpool->thread_count; ++i) {
        pthread_join(thpool->threads[i], NULL);
    }
    free(thpool->queue->buffer);
    free(thpool->queue);
    free(thpool->threads);
    free(thpool);

    return 1;
}

static void perform_tasks(thpool_t *thpool)
{
    task_t *task = NULL;
    do {
        pthread_mutex_lock(&(thpool->lock));

        while (queue_is_empty(thpool)) {
            thread_sleep(thpool);
        }

        /* grab a task from queue */
        task = queue_deq(thpool);

        pthread_mutex_unlock(&(thpool->lock));
        /* preform the task */
        if (task) {
            (task->function)(task->arg);
        }
    } while (task);
}

static void thread_sleep(thpool_t *thpool)
{
    pthread_cond_wait(&(thpool->cond), &(thpool->lock));
}

static void thread_wakeup(thpool_t *thpool)
{
    pthread_cond_signal(&(thpool->cond));
}

static void *worker_thread_cycle(void *arg)
{
    while (1) {
        perform_tasks(arg);
        sched_yield();
    }
    perform_tasks(arg);
    return 0;
}

task_t *queue_deq(thpool_t *thpool)
{
    if (thpool->queue->task_count == 0)
        return NULL;
    int tmp_offset = thpool->queue->out;
    thpool->queue->out = (thpool->queue->out + 1) % thpool->queue->size;
    --thpool->queue->task_count;
    return (thpool->queue->buffer + tmp_offset);
}

void queue_add(thpool_t *thpool, void (*task)(void *), void *arg)
{
    pthread_mutex_lock(&(thpool->lock));
    if (thpool->queue->task_count >= thpool->queue->size) {
        perror("queue is full...\n");
        pthread_mutex_unlock(&(thpool->lock));
        exit(1);
    }

    (thpool->queue->buffer + thpool->queue->in)->function = task;
    (thpool->queue->buffer + thpool->queue->in)->arg = arg;
    thpool->queue->in = (thpool->queue->in + 1) % thpool->queue->size;
    ++thpool->queue->task_count;
    thread_wakeup(thpool);

    pthread_mutex_unlock(&(thpool->lock));
}

int queue_is_empty(thpool_t *thpool)
{
    return (thpool->queue->task_count == 0) ? 1 : 0;
}