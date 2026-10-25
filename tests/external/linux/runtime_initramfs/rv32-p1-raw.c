/* Genuine RV32I/ILP32 Linux 6.6.143 P1 guest.
 * Exactly the historical twelve P1 syscall families, directly through the
 * asm-generic RISC-V Linux syscall ABI.  No host executable, GNU C-owner
 * replacement, libc, custom kernel or RV64 ABI.  This is an independent
 * userspace test of the original upstream kernel linked from 1741 MiniC C TUs.
 * The init ELF is built by the host's RV32-targeted GNU tools, as in RV64 P1.
 */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long word;
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_CLOSE 57
#define SYS_GETPID 172
#define SYS_EVENTFD2 19
#define SYS_EPOLL_CREATE1 20
#define SYS_EPOLL_CTL 21
#define SYS_EPOLL_PWAIT 22
#define SYS_TIMERFD_CREATE 85
#define SYS_TIMERFD_SETTIME64 411
#define SYS_RT_SIGPROCMASK 135
#define SYS_SIGNALFD4 74
#define SYS_KILL 129
#define SYS_INOTIFY_INIT1 26
#define SYS_INOTIFY_ADD_WATCH 27
#define SYS_OPENAT 56
#define SYS_UNLINKAT 35
#define SYS_SOCKETPAIR 199
#define SYS_SOCKET 198
#define SYS_BIND 200
#define SYS_GETSOCKNAME 204
#define SYS_LISTEN 201
#define SYS_ACCEPT4 242
#define SYS_CONNECT 203
#define SYS_CLONE 220
#define SYS_WAITID 95
#define SYS_IOCTL 29
#define SYS_MEMFD_CREATE 279
#define SYS_LSEEK 62
#define SYS_PIDFD_OPEN 434
#define SYS_FUTEX_TIME64 422
#define SYS_IO_URING_SETUP 425
#define SYS_MMAP 222
#define SYS_MPROTECT 226
#define SYS_MUNMAP 215
#define SYS_EXIT 93
#define SYS_REBOOT 142
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define EPOLL_CTL_ADD 1
#define EPOLLIN 1
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#define AT_FDCWD -100
#define IN_CREATE 256
#define SIG_BLOCK 0
#define SIGUSR1 10
#define SIGCHLD 17
#define CLOCK_MONOTONIC 1
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914

static long syscall6(long number, long p0, long p1, long p2,
                     long p3, long p4, long p5) {
    register long a0 __asm__("a0") = p0;
    register long a1 __asm__("a1") = p1;
    register long a2 __asm__("a2") = p2;
    register long a3 __asm__("a3") = p3;
    register long a4 __asm__("a4") = p4;
    register long a5 __asm__("a5") = p5;
    register long a7 __asm__("a7") = number;
    __asm__ volatile("ecall" : "+r"(a0)
                     : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a7)
                     : "memory");
    return a0;
}
#define SC0(n) syscall6(n,0,0,0,0,0,0)
#define SC1(n,a) syscall6(n,(long)(a),0,0,0,0,0)
#define SC2(n,a,b) syscall6(n,(long)(a),(long)(b),0,0,0,0)
#define SC3(n,a,b,c) syscall6(n,(long)(a),(long)(b),(long)(c),0,0,0)
#define SC4(n,a,b,c,d) syscall6(n,(long)(a),(long)(b),(long)(c),(long)(d),0,0)
#define SC5(n,a,b,c,d,e) syscall6(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
static int bad(long rv) { return (word)rv >= (word)-4095; }
static void close_fd(long fd) { if (!bad(fd)) (void)SC1(SYS_CLOSE,fd); }
static void zero(void *p, word n) {
    volatile unsigned char *v=(volatile unsigned char *)p;
    while (n--) *v++=0;
}
static word length(const char *s) { word n=0; while(s[n]) ++n; return n; }
static void say(const char *s) {
    word n=length(s);
    (void)SC3(SYS_WRITE,1,s,n);
}
static void stage(const char *name) {
    say("RV32I_P1_STAGE=");
    say(name);
    say("\n");
}
static int passed;
static int failed;
static void verdict(const char *name, int ok) {
    say(ok?"RUNTIME_V2_SYSCALL_PASS ":"RUNTIME_V2_SYSCALL_FAIL ");
    say(name);
    say("\n");
    if (ok) ++passed; else ++failed;
}
static void eventfd_test(void) {
    long fd=SC2(SYS_EVENTFD2,0,0);
    u64 value=7, got=0;
    int ok=!bad(fd) && SC3(SYS_WRITE,fd,&value,8)==8 &&
           SC3(SYS_READ,fd,&got,8)==8 && got==value;
    verdict("eventfd-rw",ok);
    close_fd(fd);
}
struct epoll_event_wire { u32 events; u64 data; } __attribute__((packed));
static void epoll_test(void) {
    long ep=SC1(SYS_EPOLL_CREATE1,0);
    long ev=SC2(SYS_EVENTFD2,0,0);
    struct epoll_event_wire request, result;
    u64 one=1;
    zero(&request,sizeof(request));zero(&result,sizeof(result));
    request.events=EPOLLIN;request.data=(u64)(word)ev;
    int ok=!bad(ep)&&!bad(ev) &&
       SC4(SYS_EPOLL_CTL,ep,EPOLL_CTL_ADD,ev,&request)==0 &&
       SC3(SYS_WRITE,ev,&one,8)==8 &&
       syscall6(SYS_EPOLL_PWAIT,ep,(long)&result,1,1000,0,8)==1 &&
       (result.events&EPOLLIN)!=0;
    verdict("epoll-eventfd",ok);
    close_fd(ev);close_fd(ep);
}
struct kts { u64 sec, nsec; };
struct kitimer { struct kts interval, value; };
static void timerfd_test(void) {
    struct kitimer t;
    u64 expirations=0;
    zero(&t,sizeof(t));t.value.nsec=1000000;
    long fd=SC2(SYS_TIMERFD_CREATE,CLOCK_MONOTONIC,0);
    /* RV32 time32 timerfd_settime expects 32-bit timespec. The common
     * asm-generic time64 syscall 411 matches kts exactly. */
    long armed=bad(fd)?-1:SC4(SYS_TIMERFD_SETTIME64,fd,0,&t,0);
    long read_n=armed==0?SC3(SYS_READ,fd,&expirations,8):-1;
    int ok=!bad(fd) && armed==0 && read_n==8 && expirations>0;
    verdict("timerfd-read",ok);
    close_fd(fd);
}
static void signalfd_test(void) {
    u64 mask=(u64)1<<(SIGUSR1-1);
    unsigned char siginfo[128];
    zero(siginfo,sizeof(siginfo));
    long r=SC4(SYS_RT_SIGPROCMASK,SIG_BLOCK,&mask,0,8);
    long fd=bad(r)?-1:SC4(SYS_SIGNALFD4,-1,&mask,8,0);
    long pid=SC0(SYS_GETPID);
    int ok=!bad(fd) && !bad(pid) &&
           SC2(SYS_KILL,pid,SIGUSR1)==0 &&
           SC3(SYS_READ,fd,siginfo,sizeof(siginfo))==(long)sizeof(siginfo) &&
           *(u32 *)siginfo==SIGUSR1;
    verdict("signalfd-read",ok);
    close_fd(fd);
}
static void inotify_test(void) {
    char buffer[512];
    long fd=SC1(SYS_INOTIFY_INIT1,0);
    long wd=bad(fd)?-1:SC3(SYS_INOTIFY_ADD_WATCH,fd,"/tmp",IN_CREATE);
    long created=bad(wd)?-1:SC4(SYS_OPENAT,AT_FDCWD,
                                 "/tmp/rv32-p1-inotify",O_CREAT|O_WRONLY|O_TRUNC,0600);
    close_fd(created);
    long n=bad(created)?-1:SC3(SYS_READ,fd,buffer,sizeof(buffer));
    int ok=!bad(fd) && !bad(wd) && !bad(created) && n>0;
    verdict("inotify-create",ok);
    (void)SC3(SYS_UNLINKAT,AT_FDCWD,"/tmp/rv32-p1-inotify",0);
    close_fd(fd);
}
static void socketpair_test(void) {
    int fds[2]={-1,-1};
    char in='S',out=0;
    long rv=SC4(SYS_SOCKETPAIR,AF_UNIX,SOCK_STREAM,0,fds);
    int ok=rv==0 &&
        SC3(SYS_WRITE,fds[0],&in,1)==1 &&
        SC3(SYS_READ,fds[1],&out,1)==1 && out==in;
    verdict("socketpair-rw",ok);
    close_fd(fds[0]);close_fd(fds[1]);
}
struct sock_addr6 {
    unsigned short family,port;
    u32 flow;
    unsigned char address[16];
    u32 scope;
};
static int loopback_up(void) {
    unsigned char ifreq[40];
    zero(ifreq,sizeof(ifreq));
    ifreq[0]='l';ifreq[1]='o';
    long fd=SC3(SYS_SOCKET,AF_INET,SOCK_DGRAM,0);
    if (bad(fd)) return 0;
    long r=SC3(SYS_IOCTL,fd,SIOCGIFFLAGS,ifreq);
    if (r==0) {
        unsigned short flags=(unsigned short)(ifreq[16]|(ifreq[17]<<8));
        flags|=65;
        ifreq[16]=(unsigned char)flags;
        ifreq[17]=(unsigned char)(flags>>8);
        r=SC3(SYS_IOCTL,fd,SIOCSIFFLAGS,ifreq);
    }
    close_fd(fd);
    return r==0;
}
static void ipv6_test(void) {
    struct sock_addr6 addr;
    u32 addrlen=sizeof(addr);
    zero(&addr,sizeof(addr));addr.family=AF_INET6;addr.address[15]=1;
    stage("ipv6-loopback-up");
    if (!loopback_up()) { stage("ipv6-loopback-up-failed"); verdict("ipv6-tcp-loopback",0); return; }
    stage("ipv6-listener-socket");
    long listener=SC3(SYS_SOCKET,AF_INET6,SOCK_STREAM,0);
    if (bad(listener)) {verdict("ipv6-tcp-loopback",0);return;}
    stage("ipv6-bind");
    long binding=SC3(SYS_BIND,listener,&addr,sizeof(addr));
    stage("ipv6-getsockname");
    long naming=binding==0?SC3(SYS_GETSOCKNAME,listener,&addr,&addrlen):-1;
    stage("ipv6-listen");
    long listening=naming==0?SC2(SYS_LISTEN,listener,1):-1;
    if (binding!=0 || naming!=0 || listening!=0) {
        close_fd(listener); verdict("ipv6-tcp-loopback",0);return;
    }
    stage("ipv6-clone");
    long child=SC5(SYS_CLONE,SIGCHLD,0,0,0,0);
    if (child==0) {
        stage("ipv6-child-socket");
        long fd=SC3(SYS_SOCKET,AF_INET6,SOCK_STREAM,0);
        char received=0;
        stage("ipv6-child-connect");
        int connected=!bad(fd) && SC3(SYS_CONNECT,fd,&addr,sizeof(addr))==0;
        stage(connected?"ipv6-child-connected":"ipv6-child-connect-failed");
        long written=connected?SC3(SYS_WRITE,fd,"V",1):-1;
        stage(written==1?"ipv6-child-write-pass":"ipv6-child-write-fail");
        long read_n=written==1?SC3(SYS_READ,fd,&received,1):-1;
        stage(read_n==1?"ipv6-child-read-pass":"ipv6-child-read-fail");
        int ok=connected && written==1 && read_n==1 && received=='6';
        stage(ok?"ipv6-child-complete-pass":"ipv6-child-complete-fail");
        close_fd(fd);
        (void)SC1(SYS_EXIT,ok?0:1);
        for(;;) __asm__ volatile("nop");
    }
    if (bad(child)) {
        verdict("ipv6-tcp-loopback",0);close_fd(listener);return;
    }
    stage("ipv6-parent-accept");
    long accepted=SC4(SYS_ACCEPT4,listener,0,0,0);
    stage(bad(accepted)?"ipv6-accept-fail":"ipv6-accept-pass");
    char got=0;
    long read_n=bad(accepted)?-1:SC3(SYS_READ,accepted,&got,1);
    stage(read_n==1?"ipv6-parent-read-pass":"ipv6-parent-read-fail");
    long written=read_n==1?SC3(SYS_WRITE,accepted,"6",1):-1;
    stage(written==1?"ipv6-parent-write-pass":"ipv6-parent-write-fail");
    close_fd(accepted);
    /* wait4(260) is an old time32 ABI and is not guaranteed on
     * RV32 Linux with only modern compat-free 32-bit syscalls.
     * waitid(95) provides the same fork/child-exit assertion without
     * timeval/rusage compatibility.  siginfo_t is 128 bytes; RV32
     * si_code at 8, si_pid at 12 and si_status at byte 20. */
    u32 info[32];
    zero(info,sizeof(info));
    long waited=SC5(SYS_WAITID,1,child,info,4,0);
    int status=(int)info[5];
    stage(waited==0?"ipv6-waitid-pass":"ipv6-waitid-fail");
    stage(info[2]==1?"ipv6-cld-exited-pass":"ipv6-cld-exited-fail");
    stage(status==0?"ipv6-child-status-pass":"ipv6-child-status-fail");
    int ok=!bad(accepted) && read_n==1 && got=='V' &&
           written==1 && waited==0 && info[2]==1 &&
           info[3]==(u32)child && status==0;
    stage(ok?"ipv6-child-parent-success":"ipv6-child-parent-failed");
    verdict("ipv6-tcp-loopback",ok);
    close_fd(listener);
}
static void memfd_test(void) {
    long fd=SC2(SYS_MEMFD_CREATE,"rv32-p1",0);
    char got=0;
    u64 position=~(u64)0;
    /* RV32 syscall 62 is sys_llseek(fd, hi, lo, *result, whence), NOT
     * RV64 sys_lseek(fd, offset, whence). */
    int ok=!bad(fd) && SC3(SYS_WRITE,fd,"M",1)==1 &&
          SC5(SYS_LSEEK,fd,0,0,&position,0)==0 && position==0 &&
          SC3(SYS_READ,fd,&got,1)==1 && got=='M';
    verdict("memfd-rw",ok);
    close_fd(fd);
}
static void pidfd_test(void) {
    long pid=SC0(SYS_GETPID);
    long fd=bad(pid)?-1:SC2(SYS_PIDFD_OPEN,pid,0);
    verdict("pidfd-open",!bad(fd));
    close_fd(fd);
}
static void futex_test(void) {
    u32 futex=0;
    /* asm-generic RV32 uses time64 entry for futex when time32 is off;
     * FUTEX_WAKE needs no timeout but still has an architecture ABI split. */
    long rv=syscall6(SYS_FUTEX_TIME64,(long)&futex,1,1,0,0,0);
    verdict("futex-wake",!bad(rv));
}
static void io_uring_test(void) {
    /* Kernel struct io_uring_params is 120 B. The extra 8 B are zero too. */
    u32 params[32];
    zero(params,sizeof(params));
    long fd=SC2(SYS_IO_URING_SETUP,2,params);
    verdict("io-uring-setup",!bad(fd));
    close_fd(fd);
}
static void mmap_test(void) {
    long ptr=syscall6(SYS_MMAP,0,4096,PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    int ok=!bad(ptr);
    if (ok) {
        volatile unsigned char *bytes=(volatile unsigned char *)(word)ptr;
        bytes[0]=0x5a;
        ok=SC3(SYS_MPROTECT,ptr,4096,PROT_READ)==0 &&
           bytes[0]==0x5a &&
           SC2(SYS_MUNMAP,ptr,4096)==0;
    }
    verdict("mmap-mprotect",ok);
}
__attribute__((noreturn,used)) void _start(void) {
    /* The historical RV64 PID1 fork/exec/wait model executes the probe in
     * a separate ELF32 address space, not inline in PID1. */
    if (SC0(SYS_GETPID)<=1) {
        say("RV32I_LINUX_P1=FAIL not_execve_child\n");
        (void)SC1(SYS_EXIT,2);
        for(;;)__asm__ volatile("nop");
    }
    say("RUNTIME_V2_SYSCALL_BEGIN\n");
    eventfd_test();
    epoll_test();
    timerfd_test();
    signalfd_test();
    inotify_test();
    socketpair_test();
    ipv6_test();
    memfd_test();
    pidfd_test();
    futex_test();
    io_uring_test();
    mmap_test();
    if (failed==0 && passed==12) {
        say("RUNTIME_V2_SYSCALL_END pass=12 fail=0\n");
        say("RV32I_LINUX_P1=PASS syscalls=12 child=ELF32 ilp32=true execve=true\n");
    } else {
        say("RV32I_LINUX_P1=FAIL syscall_suite\n");
    }
    (void)SC1(SYS_EXIT,failed==0&&passed==12?0:1);
    for(;;) __asm__ volatile("nop" ::: "memory");
}
