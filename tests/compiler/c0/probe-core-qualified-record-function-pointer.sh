#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
source_file="$root/tests/compiler/c0/core_qualified_record_function_pointer.c"
minic_cc="${MINIC_CC:-$root/build/debug/bin/minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
cc -std=gnu11 -E -P "$source_file" -o "$work/fixture.i"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/fixture.i" -o "$work/$target.s"
    test -s "$work/$target.s"
    printf 'CORE_QUALIFIED_RECORD_FUNCTION_POINTER=PASS target=%s\n' "$target"
done
as --64 -o "$work/x86_64-linux.o" "$work/x86_64-linux.s"
readelf -h "$work/x86_64-linux.o" | grep -Fq 'Advanced Micro Devices X86-64'
printf 'CORE_QUALIFIED_RECORD_FUNCTION_POINTER=PASS gnu_as=ELF64-REL\n'
