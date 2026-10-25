/* RV32 Linux 6.6.143 P0/P1 PID1. Freestanding: only Linux ILP32 syscalls.
 * Matches the historical init.c: proc/sys/devtmpfs, cmdline, tmpfs R/W,
 * fork -> execve("/bin/runtime-probe") -> waitid, clean reboot.
 * No libc or Linux-kernel ABI shortcuts. */
typedef unsigned int u32;
typedef unsigned long word;
#define SYS_CLOSE 57
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_GETPID 172
#define SYS_MOUNT 40
#define SYS_MKDIRAT 34
#define SYS_OPENAT 56
#define SYS_CLONE 220
#define SYS_EXECVE 221
#define SYS_WAITID 95
#define SYS_SYNC 81
#define SYS_REBOOT 142
#define SYS_EXIT 93
#define AT_FDCWD -100
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#define SIGCHLD 17
static long sc(long number,long p0,long p1,long p2,long p3,long p4) {
    register long a0 __asm__("a0")=p0;
    register long a1 __asm__("a1")=p1;
    register long a2 __asm__("a2")=p2;
    register long a3 __asm__("a3")=p3;
    register long a4 __asm__("a4")=p4;
    register long a7 __asm__("a7")=number;
    __asm__ volatile("ecall":"+r"(a0)
                     :"r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a7):"memory");
    return a0;
}
#define C0(n) sc(n,0,0,0,0,0)
#define C1(n,a) sc(n,(long)(a),0,0,0,0)
#define C2(n,a,b) sc(n,(long)(a),(long)(b),0,0,0)
#define C3(n,a,b,c) sc(n,(long)(a),(long)(b),(long)(c),0,0)
#define C4(n,a,b,c,d) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),0)
#define C5(n,a,b,c,d,e) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e))
static int bad(long x){return (word)x >= (word)-4095;}
static word slen(const char *s){word n=0;while(s[n])++n;return n;}
static void say(const char *s){(void)C3(SYS_WRITE,1,s,slen(s));}
static void zero(void *p,word len){unsigned char *b=p;while(len--)*b++=0;}
static int has(const char *buf,word n,const char *needle) {
    word m=slen(needle),i,j;
    if(m>n)return 0;
    for(i=0;i+m<=n;i++) {
        for(j=0;j<m&&buf[i+j]==needle[j];j++){}
        if(j==m)return 1;
    }
    return 0;
}
static int fails;
static void check(const char *marker,int ok) {
    if(ok){say(marker);say("=PASS\n");}
    else{say(marker);say("=FAIL\n");fails++;}
}
static int mount_fs(const char *type,const char *where,const char *marker) {
    (void)C3(SYS_MKDIRAT,AT_FDCWD,where,0755);
    long rc=C5(SYS_MOUNT,type,where,type,0,0);
    int ok=rc==0||rc==-16; /* already mounted (EBUSY) */
    check(marker,ok);
    return ok;
}
static int cmdline(void) {
    char line[1024];
    long fd=C4(SYS_OPENAT,AT_FDCWD,"/proc/cmdline",O_RDONLY,0);
    long n=bad(fd)?-1:C3(SYS_READ,fd,line,sizeof(line)-1);
    if(!bad(fd)) (void)C1(SYS_CLOSE,fd);
    int ok=n>0 && has(line,(word)n,"rdinit=/init");
    check("INIT_CMDLINE_READ",ok);
    /* The independently booted P0/fast profile omits the P1 selector.
     * It still checks the real procfs cmdline and all P0 functionality. */
    return ok && has(line,(word)n,"minic_runtime=p1");
}
static int tmpfs_rw(void) {
    static const char sample[]="minic-linux-runtime-v2";
    char actual[sizeof(sample)];
    if(!mount_fs("tmpfs","/tmp","INIT_TMPFS_MOUNT"))return 0;
    long fd=C4(SYS_OPENAT,AT_FDCWD,"/tmp/rv32-runtime.txt",
               O_CREAT|O_WRONLY|O_TRUNC,0600);
    int ok=!bad(fd) &&
           C3(SYS_WRITE,fd,sample,sizeof(sample))==(long)sizeof(sample);
    if(!bad(fd))(void)C1(SYS_CLOSE,fd);
    fd=C4(SYS_OPENAT,AT_FDCWD,"/tmp/rv32-runtime.txt",O_RDONLY,0);
    if(bad(fd))ok=0;
    else {
        long n=C3(SYS_READ,fd,actual,sizeof(actual));
        if(n!=(long)sizeof(sample))ok=0;
        else for(word i=0;i<sizeof(sample);++i)
            if(actual[i]!=sample[i])ok=0;
        (void)C1(SYS_CLOSE,fd);
    }
    check("INIT_TMPFS_RW",ok);
    return ok;
}
/* Run P2 once in a newly execve'd genuine ELF32 child. This is a
 * separate opt-in runtime frontier from the canonical P0/P1 oracle. */
static int p2_requested(void) {
    char line[1024];
    long fd=C4(SYS_OPENAT,AT_FDCWD,"/proc/cmdline",O_RDONLY,0);
    long n=bad(fd)?-1:C3(SYS_READ,fd,line,sizeof(line)-1);
    if(!bad(fd))(void)C1(SYS_CLOSE,fd);
    return n>0 && has(line,(word)n,"minic_runtime=p2");
}
static void run_p2(void) {
    long child=C5(SYS_CLONE,SIGCHLD,0,0,0,0);
    if(child==0) {
        char *const args[]={"runtime-p2",0};
        char *const env[]={"PATH=/bin",0};
        (void)C3(SYS_EXECVE,"/bin/runtime-p2",args,env);
        say("RV32I_P2_EXECVE=FAIL\n");
        (void)C1(SYS_EXIT,127);
        for(;;)__asm__ volatile("nop");
    }
    if(bad(child)){check("RV32I_P2_CHILD",0);return;}
    u32 info[32];zero(info,sizeof(info));
    long rv=C5(SYS_WAITID,1,child,info,4,0);
    check("RV32I_P2_CHILD",rv==0&&info[2]==1&&
          info[3]==(u32)child&&info[5]==0);
}
static int run_probe(void) {
    long child=C5(SYS_CLONE,SIGCHLD,0,0,0,0);
    if(child==0) {
        char *const args[]={"runtime-probe",0};
        char *const env[]={"PATH=/bin",0};
        (void)C3(SYS_EXECVE,"/bin/runtime-probe",args,env);
        say("INIT_RUNTIME_PROBE_EXECVE=FAIL\n");
        (void)C1(SYS_EXIT,127);
        for(;;)__asm__ volatile("nop");
    }
    if(bad(child)){
        check("INIT_RUNTIME_PROBE",0);
        return 0;
    }
    u32 info[32];
    zero(info,sizeof(info));
    /* waitid(P_PID=1, child, siginfo, WEXITED=4, NULL) */
    long rv=C5(SYS_WAITID,1,child,info,4,0);
    int ok=rv==0 && info[2]==1 && info[3]==(u32)child && info[5]==0;
    check("INIT_RUNTIME_PROBE",ok);
    return ok;
}
__attribute__((noreturn,used)) void _start(void){
    if(C0(SYS_GETPID)!=1){
        say("MINIC_LINUX_RUNTIME_INIT_END fail=1\n");
        (void)C1(SYS_EXIT,2);
        for(;;)__asm__ volatile("nop");
    }
    say("MINIC_LINUX_RUNTIME_INIT_BEGIN\n");
    (void)mount_fs("proc","/proc","INIT_PROC_MOUNT");
    (void)mount_fs("sysfs","/sys","INIT_SYSFS_MOUNT");
    (void)mount_fs("devtmpfs","/dev","INIT_DEVTMPFS_MOUNT");
    int p1=cmdline();
    int p2=p2_requested();
    (void)tmpfs_rw();
    if(p1)(void)run_probe();
    if(p2)run_p2();
    if(fails==0) {
        say("USER_SHELL_OK\n");
        say("DONE_COMMANDS\n");
        say("MINIC_LINUX_RUNTIME_INIT_END fail=0\n");
    } else {
        say("MINIC_LINUX_RUNTIME_INIT_END fail=NONZERO\n");
    }
    (void)C0(SYS_SYNC);
    (void)C4(SYS_REBOOT,0xfee1dead,0x28121969,0x4321fedc,0);
    for(;;)__asm__ volatile("nop":::"memory");
}
