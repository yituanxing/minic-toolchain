#!/usr/bin/env sh
set -eu

MINIPP=${MINIPP:?MINIPP is required}
REFERENCE_CPP=${REFERENCE_CPP:-gcc}
BUILD_DIR=${BUILD_DIR:-build}
work="$BUILD_DIR/tests/preprocessor/a0"

rm -rf "$work"
mkdir -p "$work"

reference_version="$("$REFERENCE_CPP" --version | head -n 1)"
printf 'MINIPP_REFERENCE_CPP=%s\n' "$reference_version"

run_exact() {
    name=$1
    shift
    "$REFERENCE_CPP" -E -P -undef -nostdinc -x c "$@"         "$work/$name.c" -o "$work/$name.gcc.i"
    "$MINIPP" -E -P -undef -nostdinc -x c "$@"         "$work/$name.c" -o "$work/$name.mini.i"
    if ! cmp -s "$work/$name.gcc.i" "$work/$name.mini.i"; then
        printf 'MINIPP_A0_EXACT=FAIL case=%s\n' "$name" >&2
        diff -u "$work/$name.gcc.i" "$work/$name.mini.i" >&2 || true
        return 1
    fi
    printf 'MINIPP_A0_CASE=PASS case=%s\n' "$name"
}

cat >"$work/plain.c" <<'EOF'
int plain = 7;
EOF
run_exact plain

cat >"$work/object.c" <<'EOF'
#define ANSWER 42
int answer = ANSWER;
EOF
run_exact object

cat >"$work/nested.c" <<'EOF'
#define A B
#define B 9
int nested = A;
EOF
run_exact nested

cat >"$work/conditional.c" <<'EOF'
#define ENABLED 1
#if ENABLED
int selected = ENABLED;
#else
int selected = 0;
#endif
EOF
run_exact conditional

cat >"$work/ifdef.c" <<'EOF'
#define PRESENT 1
#ifdef PRESENT
int yes = 1;
#endif
EOF
run_exact ifdef

cat >"$work/cli.c" <<'EOF'
int cli = VALUE;
EOF
run_exact cli -DVALUE=11

cat >"$work/comment.c" <<'EOF'
int/* comment */value = 3;
EOF
run_exact comment

cat >"$work/local.h" <<'EOF'
#define LOCAL_VALUE 17
int from_local_header = LOCAL_VALUE;
EOF
cat >"$work/quoted-include.c" <<'EOF'
#include "local.h"
int after_local = LOCAL_VALUE + 1;
EOF
run_exact quoted-include

mkdir -p "$work/inc"
cat >"$work/inc/angle.h" <<'EOF'
#define ANGLE_VALUE 23
int from_angle_header = ANGLE_VALUE;
EOF
cat >"$work/angle-include.c" <<'EOF'
#include <angle.h>
int after_angle = ANGLE_VALUE + 1;
EOF
run_exact angle-include -I"$work/inc"

mkdir -p "$work/nested"
cat >"$work/nested/leaf.h" <<'EOF'
#define LEAF_VALUE 31
EOF
cat >"$work/nested/root.h" <<'EOF'
#include "leaf.h"
int from_nested_header = LEAF_VALUE;
EOF
cat >"$work/nested-include.c" <<'EOF'
#include "nested/root.h"
int after_nested = LEAF_VALUE + 1;
EOF
run_exact nested-include

printf 'MINIPP_A0_EXACT=PASS cases=10 mode=byte-identical\n'
