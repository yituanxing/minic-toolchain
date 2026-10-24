/* Linux RV32I ILP32 file-descriptor boundary for authentic upstream
 * Picolibc and zlib. No custom C stdio, compression or allocator logic.
 * Linux syscalls are the only platform-specific porting surface. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>

static long linux_syscall1(long nr, long first) {
    register long a0 __asm__("a0") = first;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}
static long linux_syscall2(long nr, long first, long second) {
    register long a0 __asm__("a0") = first;
    register long a1 __asm__("a1") = second;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
    return a0;
}
static long linux_syscall3(long nr, long first, long second, long third) {
    register long a0 __asm__("a0") = first;
    register long a1 __asm__("a1") = second;
    register long a2 __asm__("a2") = third;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}
/* Linux RV32 uses __NR_llseek at asm-generic syscall 62, not the
 * 3-register LP64 lseek ABI. The kernel writes loff_t into memory. */
static long linux_syscall5(long nr, long x0, long x1, long x2,
                           long x3, long x4) {
    register long a0 __asm__("a0") = x0;
    register long a1 __asm__("a1") = x1;
    register long a2 __asm__("a2") = x2;
    register long a3 __asm__("a3") = x3;
    register long a4 __asm__("a4") = x4;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) :
        "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a7) : "memory");
    return a0;
}
static long linux_syscall4(long nr,long first,long second,long third,long fourth){
    register long a0 __asm__("a0") = first;
    register long a1 __asm__("a1") = second;
    register long a2 __asm__("a2") = third;
    register long a3 __asm__("a3") = fourth;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) :
                     "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
    return a0;
}
static long linux_result(long value) {
    if (value < 0 && value >= -4095) {
        errno = (int)-value;
        return -1;
    }
    return value;
}
ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)linux_result(linux_syscall3(
        63L, (long)fd, (long)buf, (long)count));
}
int close(int fd) {
    return (int)linux_result(linux_syscall1(57L, (long)fd));
}
off_t lseek(int fd, off_t offset, int whence) {
    const int64_t requested = (int64_t)offset;
    int64_t result = 0;
    long status = linux_syscall5(
        62L, (long)fd, (long)(requested >> 32),
        (long)(uint32_t)requested, (long)&result, (long)whence);
    if (linux_result(status) < 0) return (off_t)-1;
    /* A 32-bit off_t must not silently truncate a kernel loff_t. */
    if (sizeof(off_t) == 4U &&
        (result > (int64_t)INT32_MAX || result < (int64_t)INT32_MIN)) {
        errno = EOVERFLOW;
        return (off_t)-1;
    }
    return (off_t)result;
}
int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    return (int)linux_result(linux_syscall4(
        56L, -100L, (long)path, (long)flags, (long)mode));
}
int unlink(const char *path) {
    return (int)linux_result(linux_syscall3(
        35L, -100L, (long)path, 0L));
}

/* The portable original Lua os library exercises rename(), clock()
 * and time(), whose platform syscalls must be provided by a genuine
 * Linux RV32I/Picolibc integration rather than fabricated C stubs. */
int rename(const char *oldname, const char *newname) {
    return (int)linux_result(linux_syscall4(
        38L, -100L, (long)oldname, -100L, (long)newname));
}

/* Kernel asm-generic gettimeofday on ILP32 uses 32-bit time words;
 * Picolibc may configure time_t/suseconds_t wider. Marshal each field
 * into Picolibc's real public structure to avoid ABI size aliasing. */
int gettimeofday(struct timeval *tv, void *timezone) {
    struct {
        int32_t seconds;
        int32_t microseconds;
    } native;
    const long result = linux_result(linux_syscall2(
        169L, tv == NULL ? 0L : (long)&native, (long)timezone));
    if (result < 0) return -1;
    if (tv != NULL) {
        tv->tv_sec = (time_t)native.seconds;
        tv->tv_usec = (suseconds_t)native.microseconds;
    }
    return 0;
}

/* Linux RV32 native tms contains four 32-bit clock_t fields.
 * Picolibc's own clock_t may be wider than the kernel ABI. */
clock_t times(struct tms *buffer) {
    struct {
        int32_t utime, stime, cutime, cstime;
    } native;
    const long result = linux_result(linux_syscall1(153L, (long)&native));
    if (result < 0) return (clock_t)-1;
    if (buffer != NULL) {
        buffer->tms_utime = (clock_t)native.utime;
        buffer->tms_stime = (clock_t)native.stime;
        buffer->tms_cutime = (clock_t)native.cutime;
        buffer->tms_cstime = (clock_t)native.cstime;
    }
    return (clock_t)result;
}
