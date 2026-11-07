#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-if-condition-local-owner
source_file="$root/tests/compiler/c0/gnu_if_condition_local_owner.c"
driver_file="$root/tests/compiler/c0/gnu_if_condition_local_owner_driver.c"

mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 "$source_file" -o "$work/condition-owner.i"
"$host_cc" -O2 -S -std=gnu11 "$source_file" -o "$work/gnu.x86.s"
"$minic_cc" --target=x86_64-linux -S "$work/condition-owner.i" \
    -o "$work/minic.x86.s"
"$minic_cc" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/condition-owner.i" -o "$work/minic.rv64.s"

# This is a link-time reachability assertion, not just a runtime assertion.
# The old per-local guard regression emitted an unreachable external call in
# the RV64 object, which prevented the real Linux GNU LD vmlinux link.
if grep -F '__compiletime_assert_645' "$work/gnu.x86.s" \
        "$work/minic.rv64.s" >/dev/null; then
    printf '%s\n' 'FAIL condition-local owner: unreachable compiletime assert survives RV64 lowering' >&2
    exit 1
fi

# Host GCC invokes GNU AS and GNU LD for the MiniC assembly. x86-64 may
# retain physically dead basic blocks, so provide a stub and compare both
# runtime output and the absence of any executed assertion.
"$host_cc" -c "$work/minic.x86.s" -o "$work/minic.x86.o"
"$host_cc" -O2 -std=gnu11 -no-pie "$source_file" "$driver_file" \
    -o "$work/gnu.elf"
"$host_cc" -no-pie "$work/minic.x86.o" "$driver_file" \
    -o "$work/minic.elf"
"$work/gnu.elf" > "$work/gnu.output"
"$work/minic.elf" > "$work/minic.output"
if ! diff -u "$work/gnu.output" "$work/minic.output"; then
    printf '%s\n' 'FAIL condition-local owner: GNU/MiniC runtime mismatch' >&2
    exit 1
fi
printf '%s\n' 'PASS condition-local owner: GNU min_t + nested GNU stmt expression; RV64 unreachable symbol 0; x86 GNU AS/LD runtime differential'
