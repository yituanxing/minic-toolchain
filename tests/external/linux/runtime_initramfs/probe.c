#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/io_uring.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void pass(const char *name) {
    (void)printf("RUNTIME_V2_SYSCALL_PASS %s\n", name);
}

static void fail(const char *name) {
    (void)printf("RUNTIME_V2_SYSCALL_FAIL %s errno=%d\n", name, errno);
    ++failures;
}

static void test_eventfd(void) {
    int fd;
    uint64_t in = 7U;
    uint64_t out = 0U;

    fd = eventfd(0U, EFD_CLOEXEC);
    if (fd < 0 || write(fd, &in, sizeof(in)) != (ssize_t)sizeof(in) ||
        read(fd, &out, sizeof(out)) != (ssize_t)sizeof(out) || out != in) {
        fail("eventfd-rw");
    } else {
        pass("eventfd-rw");
    }
    if (fd >= 0) {
        (void)close(fd);
    }
}

static void test_epoll_eventfd(void) {
    int ep;
    int evfd;
    struct epoll_event event;
    struct epoll_event ready;
    uint64_t value = 1U;

    ep = epoll_create1(EPOLL_CLOEXEC);
    evfd = eventfd(0U, EFD_CLOEXEC | EFD_NONBLOCK);
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = evfd;
    memset(&ready, 0, sizeof(ready));

    if (ep < 0 || evfd < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, evfd, &event) != 0 ||
        write(evfd, &value, sizeof(value)) != (ssize_t)sizeof(value) ||
        epoll_wait(ep, &ready, 1, 1000) != 1 ||
        (ready.events & EPOLLIN) == 0) {
        fail("epoll-eventfd");
    } else {
        pass("epoll-eventfd");
    }
    if (evfd >= 0) {
        (void)close(evfd);
    }
    if (ep >= 0) {
        (void)close(ep);
    }
}

static void test_timerfd(void) {
    int fd;
    struct itimerspec spec;
    uint64_t expirations = 0U;

    fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    memset(&spec, 0, sizeof(spec));
    spec.it_value.tv_nsec = 1000000L;

    if (fd < 0 || timerfd_settime(fd, 0, &spec, NULL) != 0 ||
        read(fd, &expirations, sizeof(expirations)) != (ssize_t)sizeof(expirations) ||
        expirations == 0U) {
        fail("timerfd-read");
    } else {
        pass("timerfd-read");
    }
    if (fd >= 0) {
        (void)close(fd);
    }
}

static void test_signalfd(void) {
    int fd;
    sigset_t mask;
    struct signalfd_siginfo info;

    (void)sigemptyset(&mask);
    (void)sigaddset(&mask, SIGUSR1);
    fd = -1;
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == 0) {
        fd = signalfd(-1, &mask, SFD_CLOEXEC);
    }
    memset(&info, 0, sizeof(info));

    if (fd < 0 || kill(getpid(), SIGUSR1) != 0 ||
        read(fd, &info, sizeof(info)) != (ssize_t)sizeof(info) ||
        info.ssi_signo != SIGUSR1) {
        fail("signalfd-read");
    } else {
        pass("signalfd-read");
    }
    if (fd >= 0) {
        (void)close(fd);
    }
    (void)sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

static void test_inotify(void) {
    int fd;
    int wd;
    int created;
    char buffer[4096];
    ssize_t count;

    fd = inotify_init1(IN_CLOEXEC);
    wd = fd >= 0 ? inotify_add_watch(fd, "/tmp", IN_CREATE) : -1;
    created = open("/tmp/runtime-v2-inotify", O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (created >= 0) {
        (void)close(created);
    }
    count = fd >= 0 && wd >= 0 ? read(fd, buffer, sizeof(buffer)) : -1;

    if (fd < 0 || wd < 0 || created < 0 || count <= 0) {
        fail("inotify-create");
    } else {
        pass("inotify-create");
    }
    (void)unlink("/tmp/runtime-v2-inotify");
    if (fd >= 0) {
        (void)close(fd);
    }
}

static void test_socketpair(void) {
    int fds[2] = {-1, -1};
    char input = 'S';
    char output = 0;

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0 ||
        write(fds[0], &input, 1U) != 1 ||
        read(fds[1], &output, 1U) != 1 || output != input) {
        fail("socketpair-rw");
    } else {
        pass("socketpair-rw");
    }
    if (fds[0] >= 0) {
        (void)close(fds[0]);
    }
    if (fds[1] >= 0) {
        (void)close(fds[1]);
    }
}

static void test_ipv6_tcp(void) {
    int listener;
    int client;
    int accepted;
    pid_t child;
    int status;
    struct sockaddr_in6 address;
    socklen_t address_length;
    char value;

    listener = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
    memset(&address, 0, sizeof(address));
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = 0;
    address_length = sizeof(address);

    if (listener < 0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &address_length) != 0 ||
        listen(listener, 1) != 0) {
        fail("ipv6-tcp-loopback");
        if (listener >= 0) {
            (void)close(listener);
        }
        return;
    }

    child = fork();
    if (child == 0) {
        client = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (client < 0 ||
            connect(client, (struct sockaddr *)&address, sizeof(address)) != 0 ||
            write(client, "V", 1U) != 1 ||
            read(client, &value, 1U) != 1 || value != '6') {
            _exit(1);
        }
        (void)close(client);
        _exit(0);
    }
    if (child < 0) {
        fail("ipv6-tcp-loopback");
        (void)close(listener);
        return;
    }

    accepted = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
    value = 0;
    if (accepted < 0 || read(accepted, &value, 1U) != 1 || value != 'V' ||
        write(accepted, "6", 1U) != 1 ||
        waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fail("ipv6-tcp-loopback");
    } else {
        pass("ipv6-tcp-loopback");
    }
    if (accepted >= 0) {
        (void)close(accepted);
    }
    (void)close(listener);
}

static void test_memfd(void) {
    int fd;
    char value = 0;

    fd = (int)syscall(SYS_memfd_create, "runtime-v2", 0U);
    if (fd < 0 || write(fd, "M", 1U) != 1 || lseek(fd, 0, SEEK_SET) != 0 ||
        read(fd, &value, 1U) != 1 || value != 'M') {
        fail("memfd-rw");
    } else {
        pass("memfd-rw");
    }
    if (fd >= 0) {
        (void)close(fd);
    }
}

static void test_pidfd(void) {
#ifdef SYS_pidfd_open
    int fd = (int)syscall(SYS_pidfd_open, getpid(), 0U);
    if (fd < 0) {
        fail("pidfd-open");
    } else {
        pass("pidfd-open");
        (void)close(fd);
    }
#else
    errno = ENOSYS;
    fail("pidfd-open");
#endif
}

static void test_futex(void) {
    int word = 0;
    long result = syscall(SYS_futex, &word, FUTEX_WAKE, 1, NULL, NULL, 0);
    if (result < 0) {
        fail("futex-wake");
    } else {
        pass("futex-wake");
    }
}

static void test_io_uring(void) {
#ifdef SYS_io_uring_setup
    struct io_uring_params params;
    int fd;

    memset(&params, 0, sizeof(params));
    fd = (int)syscall(SYS_io_uring_setup, 2U, &params);
    if (fd < 0) {
        fail("io-uring-setup");
    } else {
        pass("io-uring-setup");
        (void)close(fd);
    }
#else
    errno = ENOSYS;
    fail("io-uring-setup");
#endif
}

static void test_mmap(void) {
    long page_size;
    unsigned char *memory;

    page_size = sysconf(_SC_PAGESIZE);
    memory = page_size > 0
                 ? mmap(NULL,
                        (size_t)page_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1,
                        0)
                 : MAP_FAILED;
    if (memory == MAP_FAILED) {
        fail("mmap-mprotect");
        return;
    }
    memory[0] = 0x5aU;
    if (mprotect(memory, (size_t)page_size, PROT_READ) != 0 || memory[0] != 0x5aU ||
        munmap(memory, (size_t)page_size) != 0) {
        fail("mmap-mprotect");
    } else {
        pass("mmap-mprotect");
    }
}

int main(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);

    (void)puts("RUNTIME_V2_SYSCALL_BEGIN");
    test_eventfd();
    test_epoll_eventfd();
    test_timerfd();
    test_signalfd();
    test_inotify();
    test_socketpair();
    test_ipv6_tcp();
    test_memfd();
    test_pidfd();
    test_futex();
    test_io_uring();
    test_mmap();
    (void)printf("RUNTIME_V2_SYSCALL_END pass=%d fail=%d\n", 12 - failures, failures);
    return failures == 0 ? 0 : 1;
}
