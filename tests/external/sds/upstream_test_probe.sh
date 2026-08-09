#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/sds-discovery"}
vendor="$work/upstream"
include="$work/include"
minic=${MINIC:-"$root/build/ci-release/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
upstream=5347739b1581fcba74fd5cab1fc21d2aef317d71

if test ! -f "$vendor/sds.c" || test ! -d "$include"; then
    printf '%s\n' 'FAIL external/sds-tests: run base SDS probe first' >&2
    exit 1
fi

curl -fsSL "https://raw.githubusercontent.com/antirez/sds/$upstream/testhelp.h" -o "$vendor/testhelp.h"
test "$(git hash-object "$vendor/testhelp.h")" = 450334046af86a5e0f00126f9790e9a14e170f84

cat >"$include/stdlib.h" <<'EOF'
#ifndef MINIC_SDS_STDLIB_H
#define MINIC_SDS_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void abort(void);
void exit(int status);
long strtol(const char *string, char **endptr, int base);
#endif
EOF

cat >"$include/limits.h" <<'EOF'
#ifndef MINIC_SDS_LIMITS_H
#define MINIC_SDS_LIMITS_H
#define UINT_MAX 4294967295U
#define LONG_MAX 9223372036854775807L
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX ((unsigned long long)-1)
#endif
EOF

"$host_cc" -E -P -nostdinc -std=c99 -DSDS_TEST_MAIN \
    -U__GNUC__ -U__GNUC_MINOR__ -U__GNUC_PATCHLEVEL__ \
    -I"$include" -I"$vendor" \
    "$vendor/sds.c" -o "$work/sds-test.i"

set +e
"$minic" -S "$work/sds-test.i" -o "$work/sds-test.s" \
    >"$work/sds-test.stdout" 2>"$work/sds-test.stderr"
status=$?
set -e

if test "$status" -ne 0; then
    frontier_line=$(sed -n 's/.*sds-test\.i:\([0-9][0-9]*\):.*/\1/p' "$work/sds-test.stderr" | head -n 1)
    if test -z "$frontier_line"; then
        frontier_line=1
    fi
    start_line=$((frontier_line > 12 ? frontier_line - 12 : 1))
    end_line=$((frontier_line + 12))
    printf '%s\n' "SDS_TEST_BLOCKER minic_status=$status line=$frontier_line" >&2
    printf '%s\n' "SDS_TEST_DIAGNOSTIC=$(head -n 1 "$work/sds-test.stderr")" >&2
    printf '%s\n' "SDS test frontier lines=$start_line-$end_line:" >&2
    nl -ba "$work/sds-test.i" | sed -n "${start_line},${end_line}p" >&2
    printf '%s\n' 'MiniC test diagnostic (first 24 lines):' >&2
    sed -n '1,24p' "$work/sds-test.stderr" >&2
    exit "$status"
fi

printf '%s\n' \
    "PASS external/sds-tests frontier=full-upstream-test-translation-unit upstream=$upstream"
