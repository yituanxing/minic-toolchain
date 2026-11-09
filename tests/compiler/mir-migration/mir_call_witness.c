/* Host ELF call-stack witness for the *real* typed MachineIR body writer.
 *
 * LD_PRELOAD intercepts output stdio and checks the live unwind stack for
 * minic_machine_gnu_asm_emit_body's symbol VMA. A linked-but-dead function,
 * stdout trace claim, fake assembly marker, or direct FILE* backend cannot
 * produce this witness. Nothing in the compiler-under-test is modified.
 * Does not need ptrace permissions or any cross-architecture tools.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <inttypes.h>
#include <link.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static uintptr_t machine_begin;
static uintptr_t machine_end;
static int initialized;
static int witnessed;
static __thread int inspecting;

static int executable_base(struct dl_phdr_info *info, size_t size, void *context) {
    uintptr_t *base = context;
    (void)size;
    if (info->dlpi_name != NULL && info->dlpi_name[0] == '\0') {
        *base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

/* The module writer uses mkstemp + rename for atomic publication, so the
 * final path may not exist DURING physical instruction emission. Capture
 * the live stream inode and let the caller compare it with the published
 * -o file AFTER MiniC exits. A throwaway stream cannot satisfy that check. */
static int emitted_stream_identity(FILE *stream, struct stat *identity) {
    int fd;
    return stream != NULL && identity != NULL &&
        (fd = fileno(stream)) >= 0 && fstat(fd, identity) == 0 &&
        S_ISREG(identity->st_mode);
}

static void observe_machine_body(FILE *stream) {
    void *frames[64];
    struct stat stream_identity;
    int i, count;
    if (witnessed || inspecting) return;
    inspecting = 1;
    if (!initialized) {
        const char *start = getenv("MINIC_MIR_WITNESS_START");
        const char *size = getenv("MINIC_MIR_WITNESS_SIZE");
        uintptr_t base = 0;
        initialized = 1;
        if (start == NULL || size == NULL) goto done;
        dl_iterate_phdr(executable_base, &base);
        machine_begin = base + (uintptr_t)strtoull(start, NULL, 0);
        machine_end = machine_begin + (uintptr_t)strtoull(size, NULL, 0);
        if (machine_end <= machine_begin) goto done;
    }
    if (machine_end <= machine_begin) goto done;
    count = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
    for (i = 1; i < count; ++i) {
        uintptr_t ip = (uintptr_t)frames[i];
        if (ip >= machine_begin && ip < machine_end &&
            emitted_stream_identity(stream, &stream_identity)) {
            const char *path = getenv("MINIC_MIR_WITNESS_PATH");
            if (path != NULL) {
                int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
                if (fd >= 0) {
                    char marker[160];
                    int length = snprintf(marker, sizeof(marker),
                        "typed-MIR-serializer-call %" PRIuMAX " %" PRIuMAX "\n",
                        (uintmax_t)stream_identity.st_dev,
                        (uintmax_t)stream_identity.st_ino);
                    if (length > 0 && (size_t)length < sizeof(marker))
                        (void)write(fd, marker, (size_t)length);
                    (void)close(fd);
                }
            }
            witnessed = 1;
            break;
        }
    }
done:
    inspecting = 0;
}

int fprintf(FILE *stream, const char *format, ...) {
    typedef int (*RealVfprintf)(FILE *, const char *, va_list);
    static RealVfprintf original;
    va_list args;
    int result;
    if (original == NULL)
        original = (RealVfprintf)dlsym(RTLD_NEXT, "vfprintf");
    observe_machine_body(stream);
    va_start(args, format);
    result = original(stream, format, args);
    va_end(args);
    return result;
}

int fputc(int character, FILE *stream) {
    typedef int (*RealFputc)(int, FILE *);
    static RealFputc original;
    if (original == NULL)
        original = (RealFputc)dlsym(RTLD_NEXT, "fputc");
    observe_machine_body(stream);
    return original(character, stream);
}

int fputs(const char *text, FILE *stream) {
    typedef int (*RealFputs)(const char *, FILE *);
    static RealFputs original;
    if (original == NULL)
        original = (RealFputs)dlsym(RTLD_NEXT, "fputs");
    observe_machine_body(stream);
    return original(text, stream);
}
