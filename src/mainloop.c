#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wait.h>

#include "http.h"
#include "logger.h"
#include "timer.h"

#if (ENABLE_THPOOL)
#if (THPOOL)
#include "thpool.h"
#endif
#if (LF_THPOOL)
#include "lf_thpool.h"
#endif
thpool_t *thpool;
#endif

/* the length of the struct epoll_events array pointed to by *events */
#define MAXEVENTS 1024

#define LISTENQ 1024

#define THREAD_COUNT 32
#define WORK_QUEUE_SIZE (2 << 16) /* 65536 */

/* TODO: use command line options to specify */
#define PORT 8081
#define WEBROOT "./www"

bool master_process = false;
int worker_processes = 4;

int epfd = -1;
static struct epoll_event *events;

void event_init()
{
    assert(epfd == -1);

    /* create epoll and add listenfd */
    epfd = epoll_create1(0 /* flags */);
    assert(epfd > 0 && "epoll_create1");

    events = malloc(sizeof(struct epoll_event) * MAXEVENTS);
    assert(events && "epoll_event: malloc");
}

void request_init(int listenfd)
{
    http_request_t *request = malloc(sizeof(http_request_t));
    init_http_request(request, listenfd, epfd, WEBROOT);

    struct epoll_event event = {
        .data.ptr = request,
        .events = EPOLLIN | EPOLLET | EPOLLEXCLUSIVE,
    };
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &event);
}

/* set a socket non-blocking. If a listen socket is a blocking socket, after
 * it comes out from epoll and accepts the last connection, the next accpet
 * will block unexpectedly.
 */
static int sock_set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_err("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    int s = fcntl(fd, F_SETFL, flags);
    if (s == -1) {
        log_err("fcntl");
        return -1;
    }
    return 0;
}

static int open_listenfd(int port)
{
    int listenfd, optval = 1;

    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

#if (ENABLE_SO_REUSEPORT)
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (const void *) &optval,
                   sizeof(int)) < 0)
        return -1;
#else
    /* Eliminate "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval,
                   sizeof(int)) < 0)
        return -1;
#endif

    /* Listenfd will be an endpoint for all requests to given port. */
    struct sockaddr_in serveraddr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons((unsigned short) port),
        .sin_zero = {0},
    };
    if (bind(listenfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;

    int rc UNUSED = sock_set_non_blocking(listenfd);
    assert(rc == 0 && "sock_set_non_blocking");

    return listenfd;
}

void accept_connection(int listenfd)
{
    /* we hava one or more incoming connections */
    while (1) {
        socklen_t inlen = 1;
        struct sockaddr_in clientaddr;
        int infd = accept(listenfd, (struct sockaddr *) &clientaddr, &inlen);
        if (infd < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                /* we have processed all incoming connections */
                break;
            }
            log_err("accept");
            break;
        }

        int rc UNUSED = sock_set_non_blocking(infd);
        assert(rc == 0 && "sock_set_non_blocking");

        http_request_t *request = malloc(sizeof(http_request_t));
        if (!request) {
            log_err("malloc");
            break;
        }

        init_http_request(request, infd, epfd, WEBROOT);

        struct epoll_event event;
        event.data.ptr = request;
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &event);

        add_timer(request, TIMEOUT_DEFAULT, http_close_conn);
    }
}

void process_events(int listenfd)
{
    int n = epoll_wait(epfd, events, MAXEVENTS, find_timer());
    for (int i = 0; i < n; i++) {
        http_request_t *r = events[i].data.ptr;
        int fd = r->fd;
        if (listenfd == fd) {
            accept_connection(listenfd);
        } else {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                log_err("epoll error fd: %d", r->fd);
                close(fd);
                continue;
            }
            if (!master_process) {
#if (ENABLE_THPOOL)
                thpool_enq(thpool, do_request, events[i].data.ptr);
                continue;
#endif
            }
            do_request(events[i].data.ptr);
        }
    }
}

void process_events_and_timers(int listenfd)
{
    process_events(listenfd);
    handle_expired_timers();
}

int shutdown_worker(int pid)
{
    int status;
#if (ENABLE_THPOOL)
    thpool_destroy(thpool);
#endif
    kill(pid, SIGTERM);
    waitpid(pid, &status, 0);

    if (status < 0) {
        perror("The child process terminated with error");
        return -1;
    }

    return 0;
}

void single_process_cycle(int listenfd)
{
#if (ENABLE_SO_REUSEPORT)
    listenfd = open_listenfd(PORT);
#endif
    event_init();
    timer_init();
    request_init(listenfd);

    if (!master_process) {
#if (ENABLE_THPOOL)
        _Static_assert(!(WORK_QUEUE_SIZE & (WORK_QUEUE_SIZE - 1)),
                       "WORK_QUEUE_SIZE should be power of 2");
        _Static_assert(!(THREAD_COUNT & (THREAD_COUNT - 1)),
                       "THREAD_COUNT should be power of 2");
        thpool = thpool_create(THREAD_COUNT, WORK_QUEUE_SIZE);
#endif
    }
    /* epoll_wait loop */
    printf("Worker process %d: Web server started.\n", getpid());
    while (1) {
        process_events_and_timers(listenfd);
    }
}

int spawn_process(int listenfd)
{
    int pid = fork();
    switch (pid) {
    case -1:
        log_err("spawn_process fork error");
        exit(EXIT_FAILURE);
    case 0:
        single_process_cycle(listenfd);
        exit(EXIT_SUCCESS);
    default:
        return pid;
    }
}

void master_process_cycle(int listenfd)
{
    int worker_pid[worker_processes];
    for (int i = 0; i < worker_processes; ++i) {
        worker_pid[i] = spawn_process(listenfd);
        if (worker_pid[i] <= 0) {
            log_err("Error during worker creation");
            break;
        }
    }
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGKILL);

    int result;
    sigwait(&sigset, &result);

    for (int i = 0; i < worker_processes; i++) {
        shutdown_worker(worker_pid[i]);
        printf("Shutdown worker %d\n", worker_pid[i]);
    }
}

int main()
{
    /* when a fd is closed by remote, writing to this fd will cause system
     * send SIGPIPE to this process, which exit the program
     */
    if (sigaction(SIGPIPE,
                  &(struct sigaction){.sa_handler = SIG_IGN, .sa_flags = 0},
                  NULL)) {
        log_err("Failed to install sigal handler for SIGPIPE");
        return 0;
    }

    int listenfd = -1;

#if !defined(ENABLE_SO_REUSEPORT)
    listenfd = open_listenfd(PORT);
#endif

    if (master_process) {
        master_process_cycle(listenfd);
    } else {
        single_process_cycle(listenfd);
    }
    return 0;
}
