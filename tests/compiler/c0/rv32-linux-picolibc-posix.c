/* Linux RV32I ILP32 file-descriptor boundary for authentic upstream
 * Picolibc and zlib. No custom C stdio, compression or allocator logic.
 * Linux syscalls are the only platform-specific porting surface. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

static long linux_syscall1(long nr, long first) {
    register long a0 __asm__("a0") = first;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
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
    return (off_t)linux_result(linux_syscall3(
        62L, (long)fd, (long)offset, (long)whence));
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
