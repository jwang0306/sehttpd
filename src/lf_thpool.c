#include "lf_thpool.h"

static void perform_tasks(thread_t *thread);
static void *worker_thread_cycle(void *arg);

thread_t *get_cur_thread(lf_thpool_t *lf_thpool)
{
    for (int i = 0; i < THREAD_COUNT; ++i)
        if ((lf_thpool->threads + i)->thr == pthread_self())
            return lf_thpool->threads + i;
    perror("thread not found");
    return NULL;
}

// int get_thread_id(lf_thpool_t *lf_thpool)
// {
//     for (int i = 0; i < THREAD_COUNT; ++i)
//         if ((lf_thpool->threads + i)->thread == pthread_self())
//             return i + 1;
//     return -1;
// }

lf_thpool_t *lf_thpool_create(int thread_count, int queue_size)
{
    lf_thpool_t *lf_thpool;
    lf_thpool = malloc(sizeof(lf_thpool_t) + sizeof(thread_t) * thread_count);
    lf_thpool->thread_count = thread_count;
    thread_t *thread = NULL;
    for (int i = 0; i < thread_count; ++i) {
        thread = &lf_thpool->threads[i];
        thread->id = i;
        thread->size = queue_size;
        thread->task_count = 0;
        thread->in = 0;
        thread->out = 0;
        memset(thread->buffer, 0, queue_size * sizeof(task_t));
        pthread_create(&(thread->thr), NULL, worker_thread_cycle, lf_thpool);
    }

    return lf_thpool;
}

int lf_thpool_destroy(lf_thpool_t *lf_thpool)
{
    for (int i = 0; i < lf_thpool->thread_count; ++i) {
        pthread_join((lf_thpool->threads + i)->thr, NULL);
    }
    free(lf_thpool);
    return 1;
}

task_t *lf_thpool_deq(thread_t *thread)
{
    if (!__sync_val_compare_and_swap(&(thread->task_count), 0, 0)) {
        // puts("workqueue empty");
        return NULL;
    }
    int out_offset = thread->out;
    thread->out = (thread->out + 1) % thread->size;
    __sync_fetch_and_sub(&(thread->task_count), 1);
    return thread->buffer + out_offset;
}

void lf_thpool_enq(lf_thpool_t *lf_thpool, void (*task)(void *), void *arg)
{
    thread_t *thread = round_robin_schedule(lf_thpool);
    dispatch_task(thread, task, arg);
}

thread_t *round_robin_schedule(lf_thpool_t *lf_thpool)
{
    static int cur_thread_index = -1;
    cur_thread_index = (cur_thread_index + 1) % THREAD_COUNT;
    return lf_thpool->threads + cur_thread_index;
}

int dispatch_task(thread_t *thread, void (*task)(void *), void *arg)
{
    if (__sync_val_compare_and_swap(&(thread->task_count), 0, 0) ==
        WORK_QUEUE_SIZE) {
        puts("workqueue full");
        return 0;
    }
    (thread->buffer + thread->in)->function = task;
    (thread->buffer + thread->in)->arg = arg;
    __sync_fetch_and_add(&(thread->task_count), 1);
    thread->in = (thread->in + 1) % thread->size;
    return 1;
}

static void perform_tasks(thread_t *thread)
{
    task_t *task = NULL;
    while (__sync_val_compare_and_swap(&(thread->task_count), 0, 0)) {
        task = lf_thpool_deq(thread);
        if (task) {
            (task->function)(task->arg);
        }
    }
}

static void *worker_thread_cycle(void *arg)
{
    lf_thpool_t *lf_thpool = arg;
    thread_t *thread = get_cur_thread(lf_thpool);
    while (1) {
        perform_tasks(thread);
        sched_yield();
    }
    perform_tasks(thread);
    return 0;
}