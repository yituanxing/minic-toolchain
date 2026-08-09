#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
vendor="$root/tests/vendor/cjson/upstream"
harness="$root/tests/external/cjson/runtime_harness.c"
work=${BUILD_DIR:-"$root/build/debug"}/tests/external/cjson
include="$work/include"
preprocessed="$work/cJSON.i"
assembly="$work/cJSON.s"
diagnostic="$work/minic.stderr"
binary="$work/cjson-runtime"

rm -rf "$work"
mkdir -p "$include"

verify_vendor_file() {
    name=$1
    expected_blob=$2
    path="$vendor/$name"

    if test ! -f "$path"; then
        printf '%s\n' "FAIL external/cjson: missing vendored file $path" >&2
        exit 1
    fi

    actual_blob=$(git hash-object "$path")
    if test "$actual_blob" != "$expected_blob"; then
        printf '%s\n' \
            "FAIL external/cjson: $name blob=$actual_blob expected=$expected_blob" >&2
        exit 1
    fi
}

verify_preprocessed_line() {
    line_number=$1
    expected=$2
    actual=$(sed -n "${line_number}p" "$preprocessed")

    if test "$actual" != "$expected"; then
        printf '%s\n' \
            "FAIL external/cjson: preprocessed line $line_number changed: $actual" >&2
        exit 1
    fi
}

if ! command -v "$riscv_cc" >/dev/null 2>&1; then
    printf '%s\n' "FAIL external/cjson: missing RISC-V compiler $riscv_cc" >&2
    exit 1
fi
if ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "FAIL external/cjson: missing RISC-V emulator $qemu" >&2
    exit 1
fi
if ! command -v git >/dev/null 2>&1; then
    printf '%s\n' 'FAIL external/cjson: missing git for vendor identity checks' >&2
    exit 1
fi
if test ! -f "$harness"; then
    printf '%s\n' "FAIL external/cjson: missing runtime harness $harness" >&2
    exit 1
fi

verify_vendor_file cJSON.c 6e4fb0dd369cd905923da515be87ab06db6c1ee0
verify_vendor_file cJSON.h cab5feb427725f8e5c82287f7fe59481b609b9b5
verify_vendor_file LICENSE 78deb0406d713ab9730e3c2447be1abdbd70b9a2

cat >"$include/stddef.h" <<'EOF'
#ifndef MINIC_CJSON_STDDEF_H
#define MINIC_CJSON_STDDEF_H
typedef __SIZE_TYPE__ size_t;
#define NULL ((void *)0)
#endif
EOF

cat >"$include/string.h" <<'EOF'
#ifndef MINIC_CJSON_STRING_H
#define MINIC_CJSON_STRING_H
#include <stddef.h>
size_t strlen(const char *string);
void *memcpy(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
char *strcpy(char *destination, const char *source);
int strcmp(const char *left, const char *right); int strncmp(const char *left, const char *right, size_t count);
#endif
EOF

cat >"$include/stdio.h" <<'EOF'
#ifndef MINIC_CJSON_STDIO_H
#define MINIC_CJSON_STDIO_H
int sprintf(char *buffer, const char *format, ...); int sscanf(const char *buffer, const char *format, ...);
#endif
EOF

cat >"$include/math.h" <<'EOF'
#ifndef MINIC_CJSON_MATH_H
#define MINIC_CJSON_MATH_H
double fabs(double value);
#endif
EOF

cat >"$include/stdlib.h" <<'EOF'
#ifndef MINIC_CJSON_STDLIB_H
#define MINIC_CJSON_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void free(void *pointer);
void *realloc(void *pointer, size_t size);
double strtod(const char *string, char **end_pointer);
#endif
EOF

cat >"$include/limits.h" <<'EOF'
#ifndef MINIC_CJSON_LIMITS_H
#define MINIC_CJSON_LIMITS_H
#define INT_MAX __INT_MAX__
#define INT_MIN (-__INT_MAX__ - 1)
#endif
EOF

cat >"$include/ctype.h" <<'EOF'
#ifndef MINIC_CJSON_CTYPE_H
#define MINIC_CJSON_CTYPE_H
int tolower(int character);
#endif
EOF

cat >"$include/float.h" <<'EOF'
#ifndef MINIC_CJSON_FLOAT_H
#define MINIC_CJSON_FLOAT_H
#define DBL_EPSILON 2.22044604925031308084726333618164062e-16
#endif
EOF

cat >"$include/locale.h" <<'EOF'
#ifndef MINIC_CJSON_LOCALE_H
#define MINIC_CJSON_LOCALE_H
#endif
EOF

"$riscv_cc" \
    -E -P -nostdinc -std=c11 \
    -U__GNUC__ -U__GNUC_MINOR__ -U__GNUC_PATCHLEVEL__ \
    -I"$include" -I"$vendor" \
    "$vendor/cJSON.c" \
    -o "$preprocessed"

verify_preprocessed_line 1 'typedef long unsigned int size_t;'
verify_preprocessed_line 2 'size_t strlen(const char *string);'
verify_preprocessed_line 3 'void *memcpy(void *destination, const void *source, size_t count);'
verify_preprocessed_line 4 'void *memset(void *destination, int value, size_t count);'
verify_preprocessed_line 5 'char *strcpy(char *destination, const char *source);'
verify_preprocessed_line 6 'int strcmp(const char *left, const char *right); int strncmp(const char *left, const char *right, size_t count);'
verify_preprocessed_line 7 'int sprintf(char *buffer, const char *format, ...); int sscanf(const char *buffer, const char *format, ...);'
verify_preprocessed_line 8 'double fabs(double value);'
verify_preprocessed_line 9 'void *malloc(size_t size);'
verify_preprocessed_line 10 'void free(void *pointer);'
verify_preprocessed_line 11 'void *realloc(void *pointer, size_t size);'
verify_preprocessed_line 12 'double strtod(const char *string, char **end_pointer);'
verify_preprocessed_line 13 'int tolower(int character);'
verify_preprocessed_line 14 'typedef struct cJSON'
verify_preprocessed_line 16 '    struct cJSON *next;'
verify_preprocessed_line 20 '    char *valuestring;'
verify_preprocessed_line 22 '    double valuedouble;'
verify_preprocessed_line 27 '      void *( *malloc_fn)(size_t sz);'
verify_preprocessed_line 73 'cJSON * cJSON_CreateFloatArray(const float *numbers, int count);'
verify_preprocessed_line 109 'typedef struct {'
verify_preprocessed_line 113 'static error global_error = { ((void *)0), 0 };'
verify_preprocessed_line 116 '    return (const char*) (global_error.json + global_error.position);'
verify_preprocessed_line 117 '}'
verify_preprocessed_line 122 '        return ((void *)0);'
verify_preprocessed_line 126 'double cJSON_GetNumberValue(const cJSON * const item)'
verify_preprocessed_line 127 '{'
verify_preprocessed_line 130 '        return (double) 0.0/0.0;'
verify_preprocessed_line 136 '    static char version[15];'
verify_preprocessed_line 137 '    sprintf(version, "%i.%i.%i", 1, 7, 19);'
verify_preprocessed_line 142 '    if ((string1 == ((void *)0)) || (string2 == ((void *)0)))'
verify_preprocessed_line 150 '    for(; tolower(*string1) == tolower(*string2); (void)string1++, string2++)'
verify_preprocessed_line 152 "        if (*string1 == '\\0')"
verify_preprocessed_line 165 'static internal_hooks global_hooks = { malloc, free, realloc };'
verify_preprocessed_line 174 '    length = strlen((const char*)string) + sizeof("");'
verify_preprocessed_line 175 '    copy = (unsigned char*)hooks->allocate(length);'
verify_preprocessed_line 187 '        global_hooks.allocate = malloc;'
verify_preprocessed_line 193 '    if (hooks->malloc_fn != ((void *)0))'
verify_preprocessed_line 255 '    double number = 0;'
verify_preprocessed_line 268 '        switch (((input_buffer)->content + (input_buffer)->offset)[i])'
verify_preprocessed_line 284 '                number_string_length++;'
verify_preprocessed_line 291 '                goto loop_end;'
verify_preprocessed_line 294 'loop_end:'
verify_preprocessed_line 318 '    item->valuedouble = number;'
verify_preprocessed_line 319 '    if (number >= 0x7fffffff)'
verify_preprocessed_line 329 '        item->valueint = (int)number;'
verify_preprocessed_line 332 '    input_buffer->offset += (size_t)(after_end - number_c_string);'

set +e
"$minic" -S "$preprocessed" -o "$assembly" \
    >"$work/minic.stdout" 2>"$diagnostic"
status=$?
set -e

if test "$status" -ge 128 && test -x "$root/build/ci-sanitize/bin/minic"; then
    sanitizer_diagnostic="$work/minic-sanitize.stderr"
    set +e
    ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
    UBSAN_OPTIONS=print_stacktrace=1 \
        "$root/build/ci-sanitize/bin/minic" -S "$preprocessed" -o "$work/cJSON-sanitize.s" \
        >"$work/minic-sanitize.stdout" 2>"$sanitizer_diagnostic"
    sanitizer_status=$?
    set -e
    printf '%s\n' \
        "cJSON release compiler crashed status=$status; sanitizer rerun status=$sanitizer_status" >&2
    cat "$sanitizer_diagnostic" >&2
fi

if test "$status" -ne 0; then
    printf '%s\n' "FAIL external/cjson: MiniC compile status=$status" >&2
    cat "$diagnostic" >&2
    exit 1
fi

"$riscv_cc" -std=c11 -static \
    -I"$vendor" \
    "$assembly" "$harness" \
    -lm \
    -o "$binary"

set +e
"$qemu" "$binary" >"$work/runtime.stdout" 2>"$work/runtime.stderr"
runtime_status=$?
set -e

if test "$runtime_status" -ne 0; then
    printf '%s\n' \
        "FAIL external/cjson: runtime acceptance exit=$runtime_status" >&2
    if test -s "$work/runtime.stdout"; then
        printf '%s\n' 'cJSON runtime stdout:' >&2
        cat "$work/runtime.stdout" >&2
    fi
    if test -s "$work/runtime.stderr"; then
        printf '%s\n' 'cJSON runtime stderr:' >&2
        cat "$work/runtime.stderr" >&2
    fi
    exit 1
fi

object_size=$(wc -c <"$binary" | tr -d ' ')
printf '%s\n' \
    "PASS external/cjson acceptance=parse-object-array-print-roundtrip exit=0 source=cJSON-1.7.19 offline=1 object=$object_size"
