#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/lua-discovery"}
include="$work/include"
archive="$work/lua-5.5.0.tar.gz"
vendor="$work/lua-5.5.0"
minic=${MINIC:-"$root/build/ci-release/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
release=5.5.0
sha256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d

rm -rf "$work"
mkdir -p "$work" "$include/sys"

curl -fsSL "https://www.lua.org/ftp/lua-$release.tar.gz" -o "$archive"
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
tar -xzf "$archive" -C "$work"

if test -d "$vendor/src"; then
    src="$vendor/src"
else
    src="$vendor"
fi

# Establish the real upstream reference first. This is the official released source
# package, built unchanged with the host compiler using Lua's documented build path.
make -C "$vendor" -j4 all >"$work/gcc-build.log" 2>&1
if test ! -x "$src/lua" && test ! -x "$vendor/lua"; then
    printf '%s\n' 'FAIL external/lua: GCC reference did not produce lua executable' >&2
    tail -n 100 "$work/gcc-build.log" >&2
    exit 1
fi

# The MiniC stage uses a controlled C/POSIX declaration surface. This prevents glibc
# implementation headers from becoming the first compiler frontier while leaving every
# byte of the official Lua sources and internal headers unchanged. Runtime acceptance
# will later use the real target libc/ABI, as with linenoise and SDS.
cat >"$include/stddef.h" <<'EOF'
#ifndef MINIC_LUA_STDDEF_H
#define MINIC_LUA_STDDEF_H
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#define NULL ((void *)0)
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
EOF

cat >"$include/stdint.h" <<'EOF'
#ifndef MINIC_LUA_STDINT_H
#define MINIC_LUA_STDINT_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long int64_t;
typedef unsigned long uint64_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
#define UINTPTR_MAX 18446744073709551615UL
#define SIZE_MAX 18446744073709551615UL
#endif
EOF

cat >"$include/stdarg.h" <<'EOF'
#ifndef MINIC_LUA_STDARG_H
#define MINIC_LUA_STDARG_H
typedef char *va_list;
void *__minic_va_start(void);
#define va_start(ap,last) ((ap) = (char *)__minic_va_start())
#define va_end(ap) 0
#define va_copy(dst,src) ((dst) = (src))
#define va_arg(ap,type) (*(type *)(((ap) += 8) - 8))
#endif
EOF

cat >"$include/limits.h" <<'EOF'
#ifndef MINIC_LUA_LIMITS_H
#define MINIC_LUA_LIMITS_H
#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535
#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U
#define LONG_MIN (-9223372036854775807L - 1L)
#define LONG_MAX 9223372036854775807L
#define ULONG_MAX 18446744073709551615UL
#define LLONG_MIN (-9223372036854775807LL - 1LL)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL
#endif
EOF

cat >"$include/float.h" <<'EOF'
#ifndef MINIC_LUA_FLOAT_H
#define MINIC_LUA_FLOAT_H
#define FLT_RADIX 2
#define FLT_MANT_DIG 24
#define DBL_MANT_DIG 53
#define LDBL_MANT_DIG 64
#define FLT_MAX_10_EXP 38
#define DBL_MAX_10_EXP 308
#define LDBL_MAX_10_EXP 4932
#define DBL_MIN 2.2250738585072014e-308
#define DBL_MAX 1.7976931348623157e+308
#endif
EOF

cat >"$include/stdlib.h" <<'EOF'
#ifndef MINIC_LUA_STDLIB_H
#define MINIC_LUA_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void abort(void);
void exit(int status);
char *getenv(const char *name);
int system(const char *command);
int abs(int value);
long labs(long value);
long strtol(const char *string, char **endptr, int base);
unsigned long strtoul(const char *string, char **endptr, int base);
long long strtoll(const char *string, char **endptr, int base);
unsigned long long strtoull(const char *string, char **endptr, int base);
double strtod(const char *string, char **endptr);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
#endif
EOF

cat >"$include/stdio.h" <<'EOF'
#ifndef MINIC_LUA_STDIO_H
#define MINIC_LUA_STDIO_H
#include <stddef.h>
#include <stdarg.h>
struct minic_lua_FILE { int opaque; };
typedef struct minic_lua_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#define EOF (-1)
#define BUFSIZ 8192
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FILENAME_MAX 4096
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list arguments);
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
int fputs(const char *string, FILE *stream);
int puts(const char *string);
int fputc(int character, FILE *stream);
int putchar(int character);
int fgetc(FILE *stream);
int getchar(void);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
FILE *tmpfile(void);
int fclose(FILE *stream);
int fflush(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fileno(FILE *stream);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
long ftell(FILE *stream);
int fseek(FILE *stream, long offset, int origin);
void clearerr(FILE *stream);
#endif
EOF

cat >"$include/string.h" <<'EOF'
#ifndef MINIC_LUA_STRING_H
#define MINIC_LUA_STRING_H
#include <stddef.h>
size_t strlen(const char *string);
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t count);
char *strcat(char *destination, const char *source);
char *strchr(const char *string, int character);
char *strrchr(const char *string, int character);
char *strstr(const char *string, const char *needle);
char *strerror(int error_number);
#endif
EOF

cat >"$include/ctype.h" <<'EOF'
#ifndef MINIC_LUA_CTYPE_H
#define MINIC_LUA_CTYPE_H
int isalpha(int c);
int isalnum(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);
#endif
EOF

cat >"$include/assert.h" <<'EOF'
#ifndef MINIC_LUA_ASSERT_H
#define MINIC_LUA_ASSERT_H
void abort(void);
#define assert(expression) ((expression) ? (void)0 : abort())
#endif
EOF

cat >"$include/errno.h" <<'EOF'
#ifndef MINIC_LUA_ERRNO_H
#define MINIC_LUA_ERRNO_H
int *__errno_location(void);
#define errno (*__errno_location())
#define EDOM 33
#define ERANGE 34
#define EINVAL 22
#endif
EOF

cat >"$include/math.h" <<'EOF'
#ifndef MINIC_LUA_MATH_H
#define MINIC_LUA_MATH_H
#define HUGE_VAL (1.0/0.0)
#define INFINITY (1.0/0.0)
double acos(double x); double asin(double x); double atan(double x); double atan2(double y, double x);
double ceil(double x); double cos(double x); double cosh(double x); double exp(double x); double fabs(double x);
double floor(double x); double fmod(double x, double y); double frexp(double x, int *exp); double ldexp(double x, int exp);
double log(double x); double log10(double x); double modf(double x, double *iptr); double pow(double x, double y);
double sin(double x); double sinh(double x); double sqrt(double x); double tan(double x); double tanh(double x);
int isfinite(double x); int isinf(double x); int isnan(double x);
#endif
EOF

cat >"$include/locale.h" <<'EOF'
#ifndef MINIC_LUA_LOCALE_H
#define MINIC_LUA_LOCALE_H
#define LC_ALL 6
#define LC_COLLATE 3
#define LC_CTYPE 0
#define LC_MONETARY 4
#define LC_NUMERIC 1
#define LC_TIME 2
struct lconv { char *decimal_point; };
char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);
#endif
EOF

cat >"$include/time.h" <<'EOF'
#ifndef MINIC_LUA_TIME_H
#define MINIC_LUA_TIME_H
#include <stddef.h>
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L
struct tm {
    int tm_sec; int tm_min; int tm_hour; int tm_mday; int tm_mon; int tm_year;
    int tm_wday; int tm_yday; int tm_isdst;
};
time_t time(time_t *timer);
double difftime(time_t end, time_t beginning);
time_t mktime(struct tm *timeptr);
struct tm *localtime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
size_t strftime(char *buffer, size_t size, const char *format, const struct tm *timeptr);
clock_t clock(void);
#endif
EOF

cat >"$include/setjmp.h" <<'EOF'
#ifndef MINIC_LUA_SETJMP_H
#define MINIC_LUA_SETJMP_H
typedef void *jmp_buf;
int setjmp(jmp_buf environment);
void longjmp(jmp_buf environment, int value);
#endif
EOF

cat >"$include/signal.h" <<'EOF'
#ifndef MINIC_LUA_SIGNAL_H
#define MINIC_LUA_SIGNAL_H
typedef int sig_atomic_t;
#define SIGINT 2
#define SIG_DFL ((void (*)(int))0)
void (*signal(int signal_number, void (*handler)(int)))(int);
#endif
EOF

cat >"$include/unistd.h" <<'EOF'
#ifndef MINIC_LUA_UNISTD_H
#define MINIC_LUA_UNISTD_H
#include <stddef.h>
typedef long ssize_t;
int isatty(int fd);
ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
int close(int fd);
#endif
EOF

cat >"$include/dlfcn.h" <<'EOF'
#ifndef MINIC_LUA_DLFCN_H
#define MINIC_LUA_DLFCN_H
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 256
#define RTLD_LOCAL 0
void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
char *dlerror(void);
#endif
EOF

cat >"$include/sys/types.h" <<'EOF'
#ifndef MINIC_LUA_SYS_TYPES_H
#define MINIC_LUA_SYS_TYPES_H
typedef long ssize_t;
typedef long off_t;
#endif
EOF

sources='lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c lua.c'

passed=0
for source in $sources; do
    base=${source%.c}
    "$host_cc" -E -P -nostdinc -std=c99 -DLUA_USE_LINUX \
        -U__GNUC__ -U__GNUC_MINOR__ -U__GNUC_PATCHLEVEL__ \
        -I"$include" -I"$src" \
        "$src/$source" -o "$work/$base.i"

    set +e
    "$minic" -S "$work/$base.i" -o "$work/$base.s" \
        >"$work/$base.stdout" 2>"$work/$base.stderr"
    status=$?
    set -e

    if test "$status" -ne 0; then
        frontier_line=$(sed -n "s/.*$base\\.i:\\([0-9][0-9]*\\):.*/\\1/p" "$work/$base.stderr" | head -n 1)
        if test -z "$frontier_line"; then
            frontier_line=1
        fi
        start_line=$((frontier_line > 18 ? frontier_line - 18 : 1))
        end_line=$((frontier_line + 18))
        printf '%s\n' "LUA_BLOCKER source=$source passed=$passed minic_status=$status line=$frontier_line" >&2
        printf '%s\n' "$source preprocessed frontier lines=$start_line-$end_line:" >&2
        nl -ba "$work/$base.i" | sed -n "${start_line},${end_line}p" >&2
        printf '%s\n' 'MiniC diagnostic:' >&2
        sed -n '1,120p' "$work/$base.stderr" >&2
        exit "$status"
    fi
    passed=$((passed + 1))
    printf '%s\n' "PASS external/lua-tu source=$source completed=$passed"
done

printf '%s\n' "PASS external/lua release=$release translation_units=$passed gcc_reference=full-build"
