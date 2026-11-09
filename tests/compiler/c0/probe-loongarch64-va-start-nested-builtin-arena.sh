#!/bin/sh
# Run with MINIC_CC pointing at a MODE=sanitize compiler for the ASan UAF gate.
# The same fixture also checks source/target admission with a regular build.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built MiniC compiler}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"${HOST_CC:-cc}" -E -P -nostdinc -x c \
    "$root/tests/compiler/c0/loongarch64_va_start_nested_builtin_arena.c" \
    -o "$work/repro.i"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
    "$MINIC_CC" --target=loongarch64-linux -S "$work/repro.i" \
    -o "$work/repro.s"
grep -Fq 'loong_va_start_nested_builtin:' "$work/repro.s"
grep -Fq '  cpucfg $a0, $a0' "$work/repro.s"
printf '%s\n' 'LOONG_VA_START_NESTED_BUILTIN_ARENA=PASS function_arena_growth=16_to_32 cpucfg=emitted'
