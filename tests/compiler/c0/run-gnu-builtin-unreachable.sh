#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-builtin-unreachable
assembly="$work/gnu_builtin_unreachable.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_builtin_unreachable.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'linux_bug_shape:' "$assembly" >/dev/null
grep -F 'ordinary_path:' "$assembly" >/dev/null
if grep -F '__builtin_unreachable' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_builtin_unreachable: emitted runtime builtin symbol' >&2
    exit 1
fi

cat >"$work/argument.c" <<'EOF'
void invalid(void) { __builtin_unreachable(1); }
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/argument.c" -o "$work/argument.i"
if "$minic" -S "$work/argument.i" -o "$work/argument.s" 2>"$work/argument.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_builtin_unreachable: argument accepted' >&2
    exit 1
fi
grep -F '__builtin_unreachable takes no arguments' "$work/argument.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_builtin_unreachable semantic-leaf=1 void-type=1 normalized-leaf=1 rv64-runtime-op=none args=zero-only'
