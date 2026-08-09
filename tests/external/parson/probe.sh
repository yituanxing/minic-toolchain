#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/parson-discovery"}
vendor="$work/upstream"
include="$work/include"
minic=${MINIC:-"$root/build/ci-release/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
upstream=ba29f4eda9ea7703a9f6a9cf2b0532a2605723c3

rm -rf "$work"
mkdir -p "$vendor" "$include"

curl -fsSL "https://raw.githubusercontent.com/kgabis/parson/$upstream/parson.c" -o "$vendor/parson.c"
curl -fsSL "https://raw.githubusercontent.com/kgabis/parson/$upstream/parson.h" -o "$vendor/parson.h"

test "$(git hash-object "$vendor/parson.c")" = 526aab437b418fa909517361cf39dc3dca47a8d6
test "$(git hash-object "$vendor/parson.h")" = 40be490bfd631970aad31c814de8ff5f83fe7c59

cat >"$include/stddef.h" <<'EOF'
#ifndef MINIC_PARSON_STDDEF_H
#define MINIC_PARSON_STDDEF_H
typedef __SIZE_TYPE__ size_t;
#define NULL ((void *)0)
#endif
EOF

cat >"$include/stdarg.h" <<'EOF'
#ifndef MINIC_PARSON_STDARG_H
#define MINIC_PARSON_STDARG_H
typedef void *va_list;
#define va_start(ap, last) ((void)0)
#define va_end(ap) ((void)0)
#endif
EOF

cat >"$include/stdio.h" <<'EOF'
#ifndef MINIC_PARSON_STDIO_H
#define MINIC_PARSON_STDIO_H
#include <stddef.h>
#include <stdarg.h>
struct minic_parson_FILE { int opaque; };
typedef struct minic_parson_FILE FILE;
#define SEEK_END 2
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int ferror(FILE *stream);
int fputs(const char *string, FILE *stream);
int sprintf(char *buffer, const char *format, ...);
int vsprintf(char *buffer, const char *format, va_list args);
#endif
EOF

cat >"$include/stdlib.h" <<'EOF'
#ifndef MINIC_PARSON_STDLIB_H
#define MINIC_PARSON_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void free(void *pointer);
void *realloc(void *pointer, size_t size);
double strtod(const char *string, char **end_pointer);
#endif
EOF

cat >"$include/string.h" <<'EOF'
#ifndef MINIC_PARSON_STRING_H
#define MINIC_PARSON_STRING_H
#include <stddef.h>
size_t strlen(const char *string);
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
int memcmp(const void *left, const void *right, size_t count);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strstr(const char *haystack, const char *needle);
char *strchr(const char *string, int character);
#endif
EOF

cat >"$include/ctype.h" <<'EOF'
#ifndef MINIC_PARSON_CTYPE_H
#define MINIC_PARSON_CTYPE_H
int isspace(int character);
#endif
EOF

cat >"$include/math.h" <<'EOF'
#ifndef MINIC_PARSON_MATH_H
#define MINIC_PARSON_MATH_H
#endif
EOF

cat >"$include/errno.h" <<'EOF'
#ifndef MINIC_PARSON_ERRNO_H
#define MINIC_PARSON_ERRNO_H
extern int errno;
#define ERANGE 34
#endif
EOF

"$riscv_cc" \
    -E -P -nostdinc -std=c11 \
    -U__GNUC__ -U__GNUC_MINOR__ -U__GNUC_PATCHLEVEL__ \
    -I"$include" -I"$vendor" \
    "$vendor/parson.c" -o "$work/parson.i"

set +e
"$minic" -S "$work/parson.i" -o "$work/parson.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e

if test "$status" -ne 0; then
    printf '%s\n' "PARSON_BLOCKER minic_status=$status" >&2
    sed -n '1,120p' "$work/minic.stderr" >&2
    exit "$status"
fi

printf '%s\n' 'PASS external/parson frontier=full-translation-unit source=parson-1.5.3'
