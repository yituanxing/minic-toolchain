/* Linux syscall adapter for unmodified upstream Picolibc: the libc owns
 * malloc/realloc/free, strtod, sprintf, sscanf, locale and floating logic.
 * Only the OS-specific brk/exit boundary belongs to this fixture.
 * Cross-built with external GCC RV32I/ILP32, never MiniC. */
#include <stddef.h>
#include <stdint.h>
static long linux_syscall1(long nr, long arg) {
    register long a0 __asm__("a0") = arg;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}
void *sbrk(intptr_t count) {
    uintptr_t old = (uintptr_t)linux_syscall1(214L, 0L);
    uintptr_t requested;
    if (count >= 0) {
        if ((uintptr_t)count > UINTPTR_MAX - old) return (void *)(intptr_t)-1;
        requested = old + (uintptr_t)count;
    } else {
        uintptr_t decrease = (uintptr_t)(-(count + 1)) + 1U;
        if (decrease > old) return (void *)(intptr_t)-1;
        requested = old - decrease;
    }
    if ((uintptr_t)linux_syscall1(214L, (long)requested) != requested)
        return (void *)(intptr_t)-1;
    return (void *)old;
}
__attribute__((noreturn)) void _exit(int code) {
    (void)linux_syscall1(93L, code);
    for (;;) { __asm__ volatile("j ."); }
}
