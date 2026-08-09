#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/linenoise-discovery"}
vendor="$work/upstream"
include="$work/include"
minic=${MINIC:-"$root/build/ci-release/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
upstream=a473823d74b93eab2ba83480df16ed37617493f2

rm -rf "$work"
mkdir -p "$vendor" "$include/sys"

curl -fsSL "https://raw.githubusercontent.com/antirez/linenoise/$upstream/linenoise.c" -o "$vendor/linenoise.c"
curl -fsSL "https://raw.githubusercontent.com/antirez/linenoise/$upstream/linenoise.h" -o "$vendor/linenoise.h"

test "$(git hash-object "$vendor/linenoise.c")" = 63f23ddaf0e06dea4d2ac04efa084c3ca275ad8c
test "$(git hash-object "$vendor/linenoise.h")" = 735629b78ed2302d407fb3b6c8e56c6ac24bd6b7

# Establish a real compiler reference with the normal host libc/POSIX headers.
"$host_cc" -std=gnu11 -O2 -I"$vendor" -c "$vendor/linenoise.c" -o "$work/linenoise-gcc.o"

# The first discovery run proved that normal glibc preprocessing stops MiniC in
# libc-internal `unsigned short` typedefs before linenoise itself is reached.
# Keep the upstream translation unit unchanged, but give the compiler stage the
# declarations the source actually consumes. This is a compile-discovery ABI
# surface only; target runtime validation will use the real RV64 libc ABI.
cat >"$include/stddef.h" <<'EOF'
#ifndef MINIC_LINENOISE_STDDEF_H
#define MINIC_LINENOISE_STDDEF_H
typedef __SIZE_TYPE__ size_t;
#define NULL ((void *)0)
#endif
EOF

cat >"$include/stdint.h" <<'EOF'
#ifndef MINIC_LINENOISE_STDINT_H
#define MINIC_LINENOISE_STDINT_H
typedef unsigned int uint32_t;
#endif
EOF

cat >"$include/sys/types.h" <<'EOF'
#ifndef MINIC_LINENOISE_SYS_TYPES_H
#define MINIC_LINENOISE_SYS_TYPES_H
typedef unsigned int mode_t;
typedef long ssize_t;
#endif
EOF

cat >"$include/termios.h" <<'EOF'
#ifndef MINIC_LINENOISE_TERMIOS_H
#define MINIC_LINENOISE_TERMIOS_H
struct termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_cc[32];
};
#define BRKINT 1
#define ICRNL 2
#define INPCK 4
#define ISTRIP 8
#define IXON 16
#define OPOST 32
#define CS8 64
#define ECHO 128
#define ICANON 256
#define IEXTEN 512
#define ISIG 1024
#define VMIN 0
#define VTIME 1
#define TCSAFLUSH 2
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
#endif
EOF

cat >"$include/unistd.h" <<'EOF'
#ifndef MINIC_LINENOISE_UNISTD_H
#define MINIC_LINENOISE_UNISTD_H
#include <stddef.h>
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
int isatty(int fd);
#endif
EOF

cat >"$include/stdlib.h" <<'EOF'
#ifndef MINIC_LINENOISE_STDLIB_H
#define MINIC_LINENOISE_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
char *getenv(const char *name);
int atexit(void (*function)(void));
int atoi(const char *string);
#endif
EOF

cat >"$include/stdio.h" <<'EOF'
#ifndef MINIC_LINENOISE_STDIO_H
#define MINIC_LINENOISE_STDIO_H
#include <stddef.h>
struct minic_linenoise_FILE { int opaque; };
typedef struct minic_linenoise_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
#define EOF (-1)
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fgetc(FILE *stream);
int fputs(const char *string, FILE *stream);
int fprintf(FILE *stream, const char *format, ...);
int printf(const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int sscanf(const char *string, const char *format, ...);
int fflush(FILE *stream);
int fileno(FILE *stream);
#endif
EOF

cat >"$include/errno.h" <<'EOF'
#ifndef MINIC_LINENOISE_ERRNO_H
#define MINIC_LINENOISE_ERRNO_H
int *__errno_location(void);
#define errno (*__errno_location())
#define ENOENT 2
#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#define ENOMEM 12
#define ENOTTY 25
#endif
EOF

cat >"$include/string.h" <<'EOF'
#ifndef MINIC_LINENOISE_STRING_H
#define MINIC_LINENOISE_STRING_H
#include <stddef.h>
size_t strlen(const char *string);
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strdup(const char *string);
#endif
EOF

cat >"$include/ctype.h" <<'EOF'
#ifndef MINIC_LINENOISE_CTYPE_H
#define MINIC_LINENOISE_CTYPE_H
int isprint(int character);
#endif
EOF

cat >"$include/sys/stat.h" <<'EOF'
#ifndef MINIC_LINENOISE_SYS_STAT_H
#define MINIC_LINENOISE_SYS_STAT_H
#include <sys/types.h>
#define S_IXUSR 64
#define S_IRWXG 56
#define S_IRWXO 7
#define S_IRUSR 256
#define S_IWUSR 128
mode_t umask(mode_t mask);
int fchmod(int fd, mode_t mode);
#endif
EOF

cat >"$include/sys/ioctl.h" <<'EOF'
#ifndef MINIC_LINENOISE_SYS_IOCTL_H
#define MINIC_LINENOISE_SYS_IOCTL_H
struct winsize {
    unsigned int ws_row;
    unsigned int ws_col;
};
#define TIOCGWINSZ 21523
int ioctl(int fd, unsigned long request, void *argument);
#endif
EOF

"$host_cc" \
    -E -P -nostdinc -std=gnu11 \
    -U__GNUC__ -U__GNUC_MINOR__ -U__GNUC_PATCHLEVEL__ \
    -I"$include" -I"$vendor" \
    "$vendor/linenoise.c" -o "$work/linenoise.i"

set +e
"$minic" -S "$work/linenoise.i" -o "$work/linenoise.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e

if test "$status" -ne 0; then
    frontier_line=$(sed -n 's/.*linenoise\.i:\([0-9][0-9]*\):.*/\1/p' "$work/minic.stderr" | head -n 1)
    if test -z "$frontier_line"; then
        frontier_line=1
    fi
    start_line=$((frontier_line > 24 ? frontier_line - 24 : 1))
    end_line=$((frontier_line + 24))
    printf '%s\n' "LINENOISE_BLOCKER minic_status=$status line=$frontier_line" >&2
    printf '%s\n' "linenoise preprocessed frontier lines=$start_line-$end_line:" >&2
    nl -ba "$work/linenoise.i" | sed -n "${start_line},${end_line}p" >&2
    printf '%s\n' 'MiniC diagnostic:' >&2
    sed -n '1,160p' "$work/minic.stderr" >&2
    exit "$status"
fi

printf '%s\n' \
    "PASS external/linenoise frontier=full-translation-unit upstream=$upstream gcc_reference=object"
