#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-builtin-constant-p-if-guard
source_file="$root/tests/compiler/c0/gnu_builtin_constant_p_if_guard.c"
driver_file="$root/tests/compiler/c0/gnu_builtin_constant_p_if_guard_driver.c"

mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 "$source_file" -o "$work/guard.i"
"$minic_cc" --target=x86_64-linux -S "$work/guard.i" -o "$work/guard.x86.s"
"$minic_cc" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/guard.i" -o "$work/guard.rv64.s"

if grep -E '(call|callq|jal|bl).*(__min_compiletime_assert|__clamp_compiletime_assert)' \
        "$work/guard.x86.s" "$work/guard.rv64.s" >/dev/null; then
    printf '%s\n' 'FAIL builtin_constant_p: unreachable compile-time assert call emitted' >&2
    exit 1
fi
# The runtime guard remains reachable; do not turn purity into global DCE.
grep -F 'ordinary_runtime_call' "$work/guard.x86.s" >/dev/null
grep -F 'ordinary_runtime_call' "$work/guard.rv64.s" >/dev/null

"$host_cc" -O2 -std=gnu11 -no-pie "$source_file" "$driver_file" \
    -o "$work/gnu.elf"
"$host_cc" -no-pie "$work/guard.x86.s" "$driver_file" -o "$work/minic.elf"
"$work/gnu.elf" > "$work/gnu.output"
"$work/minic.elf" > "$work/minic.output"
if ! diff -u "$work/gnu.output" "$work/minic.output"; then
    printf '%s\n' 'FAIL builtin_constant_p: GNU/MiniC output mismatch' >&2
    exit 1
fi
printf '%s\n' 'PASS builtin_constant_p if-guard GNU/MiniC runtime differential x86-64; RV64 symbolic reachability'
