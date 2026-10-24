/* Linux syscall adapter for unmodified upstream Picolibc: the libc owns
 * malloc/realloc/free, strtod, sprintf, sscanf, locale and floating logic.
 * Only the OS-specific brk/exit boundary belongs to this fixture.
 * Cross-built with external GCC RV32I/ILP32, never MiniC. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
/* RISC-V local-exec TLS belongs to the process entry, not to a fake
 * implementation of Picolibc errno. The Linux kernel maps PT_TLS but does
 * not construct the userspace thread pointer. Our single-threaded static
 * ELF32 test adapter copies the REAL linked TLS template and zero-fills
 * tbss, before calling any libc initialization or application code.
 *
 * The initial Linux stack contains argc, argv[], envp[] and auxv pairs.
 * Discover PT_TLS through AT_PHDR/AT_PHNUM (no private libc internals).
 * Missing/malformed metadata or unsupported TLS size fails closed. */
typedef struct {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} Rv32ElfProgramHeader;
_Static_assert(sizeof(Rv32ElfProgramHeader) == 32U, "ELF32 phdr ABI");

#define RV32_STATIC_TLS_CAPACITY 4096U
#define RV32_STATIC_TLS_ALIGNMENT 64U
static unsigned char rv32_static_tls[RV32_STATIC_TLS_CAPACITY +
                                    RV32_STATIC_TLS_ALIGNMENT]
    __attribute__((aligned(RV32_STATIC_TLS_ALIGNMENT)));

void *rv32_picolibc_tls_init(const uintptr_t *entry_stack) {
    const uintptr_t argc = entry_stack[0];
    const uintptr_t *cursor;
    uintptr_t phdr_address = 0U, phdr_count = 0U, phdr_size = 0U;
    uintptr_t phdr_index;
    int found = 0;
    if (argc > 4096U || entry_stack[argc + 1U] != 0U) _exit(127);
    cursor = entry_stack + argc + 2U;
    while (*cursor != 0U) ++cursor;  /* environment pointers */
    ++cursor;
    while (cursor[0] != 0U) {       /* AT_NULL */
        if (cursor[0] == 3U) phdr_address = cursor[1]; /* AT_PHDR */
        if (cursor[0] == 4U) phdr_size = cursor[1];    /* AT_PHENT */
        if (cursor[0] == 5U) phdr_count = cursor[1];   /* AT_PHNUM */
        cursor += 2;
    }
    if (phdr_address == 0U || phdr_count == 0U || phdr_count > 256U ||
        phdr_size != sizeof(Rv32ElfProgramHeader)) _exit(127);
    for (phdr_index = 0U; phdr_index < phdr_count; ++phdr_index) {
        const Rv32ElfProgramHeader *header =
            (const Rv32ElfProgramHeader *)(phdr_address +
                phdr_index * phdr_size);
        unsigned char *dest;
        const unsigned char *src;
        uintptr_t i, align, base;
        if (header->type != 7U) continue;  /* PT_TLS */
        if (found) _exit(127);
        found = 1;
        align = header->align == 0U ? 1U : header->align;
        if ((align & (align - 1U)) != 0U ||
            align > RV32_STATIC_TLS_ALIGNMENT ||
            header->filesz > header->memsz ||
            header->memsz > RV32_STATIC_TLS_CAPACITY) _exit(127);
        base = ((uintptr_t)rv32_static_tls + align - 1U) & ~(align - 1U);
        dest = (unsigned char *)base;
        src = (const unsigned char *)(uintptr_t)header->vaddr;
        for (i = 0U; i < header->filesz; ++i) dest[i] = src[i];
        for (; i < header->memsz; ++i) dest[i] = 0U;
        return dest;
    }
    /* With no PT_TLS, tp is not dereferenced by any legitimate TLS access. */
    return rv32_static_tls;
}

static long linux_syscall1(long nr, long arg) {
    register long a0 __asm__("a0") = arg;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}
/* The Debian Picolibc tinystdio build leaves stdout/stdin/stderr as
 * target-platform integration points. Supply the documented FILE
 * callbacks backed by the Linux syscall adapter, not a substitute
 * implementation of printf, snprintf or string formatting. */
static int rv32_linux_console_put(char ch, FILE *file) {
    (void)file;
    return write(1, &ch, 1U) == 1 ? (unsigned char)ch : EOF;
}
static int rv32_linux_console_get(FILE *file) {
    (void)file;
    return EOF;
}
static FILE rv32_linux_console = FDEV_SETUP_STREAM(
    rv32_linux_console_put, rv32_linux_console_get, NULL, _FDEV_SETUP_RW);
FILE *const stdin = &rv32_linux_console;
FILE *const stdout = &rv32_linux_console;
FILE *const stderr = &rv32_linux_console;

/* Picolibc's tinystdio and the original upstream test.c use a real
 * Linux write(2), not a test-only output simulator. */
ssize_t write(int fd, const void *buffer, size_t length) {
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buffer;
    register long a2 __asm__("a2") = (long)length;
    register long a7 __asm__("a7") = 64L;
    __asm__ volatile("ecall" : "+r"(a0) :
                     "r"(a1), "r"(a2), "r"(a7) : "memory");
    return (ssize_t)a0;
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
