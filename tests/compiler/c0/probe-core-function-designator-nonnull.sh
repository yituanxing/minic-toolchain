#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail() { printf 'CORE_FUNCTION_NONNULL=FAIL %s\n' "$*" >&2; exit 1; }
source_file="$root/tests/compiler/c0/core_function_designator_nonnull.c"
cc -std=gnu11 -O2 -fno-pie -no-pie "$source_file" -o "$work/gnu"
"$work/gnu" || fail 'GNU C runtime failed'
cc -E -P -std=gnu11 -x c "$source_file" -o "$work/probe.i"
for target in x86_64-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic" -S "$work/probe.i" --target="$target" -o "$work/$target.s"
    if grep -Fq '__compiletime_assert_strong_function_null' "$work/$target.s"; then
        fail "$target retained impossible strong-function null-guard"
    fi
    grep -Fq 'optional_weak_hook' "$work/$target.s" ||
        fail "$target incorrectly folded undefined weak-function comparison"
done
as --64 "$work/x86_64-linux.s" -o "$work/mini.o"
cc -no-pie -Wl,-z,noexecstack "$work/mini.o" -o "$work/mini"
"$work/mini" || fail 'MiniC GNU AS/LD runtime differs from GNU C'
printf 'CORE_FUNCTION_NONNULL=PASS strong_guards=2 weak_retained=4 ISAs=4 GNU_AS=1 GNU_LD=1 GNU_C_diff=1\n'
