#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-gnu-auto-type-local"

mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_auto_type_local.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

grep -F "linux_max_shape:" "$work/output.s" >/dev/null
grep -F "initializer_scope:" "$work/output.s" >/dev/null
grep -F "pointer_inference:" "$work/output.s" >/dev/null

cat >"$work/missing-initializer.c" <<'EOF'
int f(void) { __auto_type value; return 0; }
EOF
"$host_cc" -E -P -x c "$work/missing-initializer.c" -o "$work/missing-initializer.i"
if "$minic" -S "$work/missing-initializer.i" -o "$work/missing-initializer.s" 2>"$work/missing-initializer.stderr"; then
    printf '%s\n' "FAIL compiler/c0/gnu_auto_type_local: missing initializer accepted" >&2
    exit 1
fi
grep -F "GNU __auto_type declaration requires an initializer" "$work/missing-initializer.stderr" >/dev/null

cat >"$work/multiple.c" <<'EOF'
int f(void) { __auto_type first = 1, second = 2; return first + second; }
EOF
"$host_cc" -E -P -x c "$work/multiple.c" -o "$work/multiple.i"
if "$minic" -S "$work/multiple.i" -o "$work/multiple.s" 2>"$work/multiple.stderr"; then
    printf '%s\n' "FAIL compiler/c0/gnu_auto_type_local: multiple declarators accepted" >&2
    exit 1
fi
grep -F "expected ';' after GNU __auto_type declaration" "$work/multiple.stderr" >/dev/null

printf '%s\n' "PASS compiler/c0/gnu_auto_type_local inference=initializer scope=after-initializer linux-max-shape=1 pointer=1 single-declarator=1 initialized=1"
