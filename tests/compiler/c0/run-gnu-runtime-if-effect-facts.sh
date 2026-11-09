#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-runtime-if-effect-facts
source_file="$root/tests/compiler/c0/gnu_runtime_if_effect_facts.c"

mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 "$source_file" -o "$work/effects.i"
"$minic_cc" --target=x86_64-linux -S "$work/effects.i" -o "$work/effects.x86.s"
"$minic_cc" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/effects.i" -o "$work/effects.rv64.s"
"$host_cc" -O2 -std=gnu11 -no-pie "$source_file" -o "$work/gnu.elf"
"$host_cc" -no-pie "$work/effects.x86.s" -o "$work/minic.elf"
"$work/gnu.elf" > "$work/gnu.output"
"$work/minic.elf" > "$work/minic.output"
if ! diff -u "$work/gnu.output" "$work/minic.output"; then
    printf '%s\n' 'FAIL runtime IF per-local effect facts GNU/MiniC differential' >&2
    exit 1
fi
printf '%s\n' 'PASS runtime IF per-local effect facts: volatile/call/conditional-assign/alias/loop/phi/short-circuit/unevaluated; RV64 assembly'
