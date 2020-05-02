#include "lf_thpool.h"

static void perform_tasks(thread_t *thread);
static void *worker_thread_cycle(void *arg);
static void sig_do_nothing();

static pthread_t main_tid;
static volatile int global_thread_num = 0;

static void sig_do_nothing()
{
    return;
}

void thread_registration(int num_expected)
{
    sigset_t signal_mask, oldmask;
    int sig_caught;

    sigemptyset(&oldmask);
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);

    while (__sync_val_compare_and_swap(&global_thread_num, 0, 0) <
           num_expected) {
        sigwait(&signal_mask, &sig_caught);
    }
    pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
}

thread_t *get_cur_thread(thpool_t *lf_thpool)
{
    for (int i = 0; i < lf_thpool->thread_count; ++i)
        if ((lf_thpool->threads + i)->thr == pthread_self())
            return lf_thpool->threads + i;
    perror("thread not found");
    return NULL;
}

#if 0
int get_thread_id(thpool_t *lf_thpool)
{
    for (int i = 0; i < THREAD_COUNT; ++i)
        if ((lf_thpool->threads + i)->thread == pthread_self())
            return i + 1;
    return -1;
}
#endif

thpool_t *thpool_create(int thread_count, int queue_size)
{
    thpool_t *lf_thpool;
    lf_thpool = (thpool_t *) malloc(sizeof(thpool_t));
    lf_thpool->threads = (thread_t *) malloc(sizeof(thread_t) * thread_count);
    lf_thpool->thread_count = thread_count;
    thread_t *thread = NULL;

    // register signal
    global_thread_num = thread_count;
    signal(SIGUSR1, sig_do_nothing);
    main_tid = pthread_self();

    for (int i = 0; i < thread_count; ++i) {
        thread = &lf_thpool->threads[i];
        thread->id = i;
        thread->size = queue_size / thread_count;
        thread->task_count = 0;
        thread->in = 0;
        thread->out = 0;
        thread->buffer = (task_t *) malloc(sizeof(task_t) * thread->size);
        pthread_create(&(thread->thr), NULL, worker_thread_cycle, lf_thpool);
    }

    thread_registration(thread_count);

    return lf_thpool;
}

int thpool_destroy(thpool_t *lf_thpool)
{
    for (int i = 0; i < lf_thpool->thread_count; ++i) {
        pthread_join((lf_thpool->threads + i)->thr, NULL);
        free((lf_thpool->threads + i)->buffer);
    }
    free(lf_thpool->threads);
    free(lf_thpool);
    return 1;
}

task_t *thpool_deq(thread_t *thread)
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

void thpool_enq(thpool_t *lf_thpool, void (*task)(void *), void *arg)
{
    thread_t *thread = round_robin_schedule(lf_thpool);
    dispatch_task(thread, task, arg);
    if (__sync_val_compare_and_swap(&(thread->task_count), 0, 0) == 1) {
        pthread_kill(thread->thr, SIGUSR1);
    }
}

thread_t *round_robin_schedule(thpool_t *lf_thpool)
{
    static int cur_thread_index = -1;
    cur_thread_index = (cur_thread_index + 1) % lf_thpool->thread_count;
    return lf_thpool->threads + cur_thread_index;
}

int dispatch_task(thread_t *thread, void (*task)(void *), void *arg)
{
    if (__sync_val_compare_and_swap(&(thread->task_count), 0, 0) ==
        thread->size) {
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
    do {
        task = thpool_deq(thread);
        if (task)
            (task->function)(task->arg);
    } while (task);
}

static void *worker_thread_cycle(void *arg)
{
    sigset_t signal_mask, oldmask;
    int sig_caught;

    thpool_t *lf_thpool = arg;
    thread_t *thread = get_cur_thread(lf_thpool);
    __sync_fetch_and_add(&global_thread_num, 1);
    pthread_kill(main_tid, SIGUSR1);

    sigemptyset(&oldmask);
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGUSR1);

    while (1) {
        pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
        while (!__sync_val_compare_and_swap(&(thread->task_count), 0, 0)) {
            // puts("thread wait for signal");
            sigwait(&signal_mask, &sig_caught);
        }
        pthread_sigmask(SIG_SETMASK, &oldmask, NULL);

        perform_tasks(thread);
        sched_yield();
    }
    perform_tasks(thread);
    return 0;
}