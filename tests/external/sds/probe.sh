#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/sds-discovery"}
vendor="$work/upstream"
include="$work/include"
minic=${MINIC:-"$root/build/ci-release/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
upstream=5347739b1581fcba74fd5cab1fc21d2aef317d71

rm -rf "$work"
mkdir -p "$vendor" "$include/sys"

curl -fsSL "https://raw.githubusercontent.com/antirez/sds/$upstream/sds.c" -o "$vendor/sds.c"
curl -fsSL "https://raw.githubusercontent.com/antirez/sds/$upstream/sds.h" -o "$vendor/sds.h"
curl -fsSL "https://raw.githubusercontent.com/antirez/sds/$upstream/sdsalloc.h" -o "$vendor/sdsalloc.h"

test "$(git hash-object "$vendor/sds.c")" = 3a7eae72f7591b3669af73954c42088ebbeccc4f
test "$(git hash-object "$vendor/sds.h")" = adcc12c0a7646d2c88a796ad5591931017140999
test "$(git hash-object "$vendor/sdsalloc.h")" = f43023c48438961445f6064ae8d0cc25f2b42f21

"$host_cc" -std=c99 -pedantic -O2 -Wall -I"$vendor" \
    -c "$vendor/sds.c" -o "$work/sds-gcc.o"

cat >"$include/stddef.h" <<'EOF'
#ifndef MINIC_SDS_STDDEF_H
#define MINIC_SDS_STDDEF_H
typedef __SIZE_TYPE__ size_t;
#define NULL ((void *)0)
#endif
EOF

cat >"$include/sys/types.h" <<'EOF'
#ifndef MINIC_SDS_SYS_TYPES_H
#define MINIC_SDS_SYS_TYPES_H
typedef long ssize_t;
#endif
EOF

cat >"$include/stdint.h" <<'EOF'
#ifndef MINIC_SDS_STDINT_H
#define MINIC_SDS_STDINT_H
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
#endif
EOF

cat >"$include/stdarg.h" <<'EOF'
#ifndef MINIC_SDS_STDARG_H
#define MINIC_SDS_STDARG_H
typedef char *va_list;
void *__minic_va_start(void);
#define va_start(ap,last) ((ap) = (char *)__minic_va_start())
#define va_end(ap) 0
#define va_copy(dst,src) ((dst) = (src))
#define va_arg(ap,type) (*(type *)(((ap) += 8) - 8))
#endif
EOF

cat >"$include/stdlib.h" <<'EOF'
#ifndef MINIC_SDS_STDLIB_H
#define MINIC_SDS_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void abort(void);
long strtol(const char *string, char **endptr, int base);
#endif
EOF

cat >"$include/stdio.h" <<'EOF'
#ifndef MINIC_SDS_STDIO_H
#define MINIC_SDS_STDIO_H
#include <stddef.h>
#include <stdarg.h>
int printf(const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
#endif
EOF

cat >"$include/string.h" <<'EOF'
#ifndef MINIC_SDS_STRING_H
#define MINIC_SDS_STRING_H
#include <stddef.h>
size_t strlen(const char *string);
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
int strcmp(const char *left, const char *right);
char *strchr(const char *string, int character);
#endif
EOF

cat >"$include/ctype.h" <<'EOF'
#ifndef MINIC_SDS_CTYPE_H
#define MINIC_SDS_CTYPE_H
int isspace(int character);
int tolower(int character);
#endif
EOF

cat >"$include/assert.h" <<'EOF'
#ifndef MINIC_SDS_ASSERT_H
#define MINIC_SDS_ASSERT_H
void abort(void);
#define assert(expression) ((expression) ? (void)0 : abort())
#endif
EOF

cat >"$include/limits.h" <<'EOF'
#ifndef MINIC_SDS_LIMITS_H
#define MINIC_SDS_LIMITS_H
#define LONG_MAX 9223372036854775807L
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)
#endif
EOF

"$host_cc" -E -P -nostdinc -std=c99 \
    -I"$include" -I"$vendor" \
    "$vendor/sds.c" -o "$work/sds.i"

set +e
"$minic" -S "$work/sds.i" -o "$work/sds.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e

if test "$status" -ne 0; then
    frontier_line=$(sed -n 's/.*sds\.i:\([0-9][0-9]*\):.*/\1/p' "$work/minic.stderr" | head -n 1)
    if test -z "$frontier_line"; then
        frontier_line=1
    fi
    start_line=$((frontier_line > 28 ? frontier_line - 28 : 1))
    end_line=$((frontier_line + 28))
    printf '%s\n' "SDS_BLOCKER minic_status=$status line=$frontier_line" >&2
    printf '%s\n' "SDS preprocessed frontier lines=$start_line-$end_line:" >&2
    nl -ba "$work/sds.i" | sed -n "${start_line},${end_line}p" >&2
    printf '%s\n' 'MiniC diagnostic:' >&2
    sed -n '1,180p' "$work/minic.stderr" >&2
    exit "$status"
fi

printf '%s\n' \
    "PASS external/sds frontier=full-translation-unit upstream=$upstream gcc_reference=object"
