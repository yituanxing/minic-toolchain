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

# These are invalid source-language pointer conversions. Core must never
# silently equate const/volatile pointees or independent nominal records
# solely because their machine pointers have the same size and alignment.
for negative in core_qualified_pointer_negative_const core_qualified_pointer_negative_nominal; do
    negative_file="$root/tests/compiler/c0/$negative.c"
    if cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$negative_file" >/dev/null 2>&1; then
        printf 'CORE_QUALIFIED_POINTER_NEGATIVE=FAIL host=%s\n' "$negative" >&2
        exit 1
    fi
    cc -std=gnu11 -E -P "$negative_file" -o "$work/$negative.i"
    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        if "$minic_cc" --target="$target" -S "$work/$negative.i" \
            -o "$work/$negative-$target.s" >/dev/null 2>&1; then
            printf 'CORE_QUALIFIED_POINTER_NEGATIVE=FAIL target=%s case=%s\n' "$target" "$negative" >&2
            exit 1
        fi
        printf 'CORE_QUALIFIED_POINTER_NEGATIVE=PASS target=%s case=%s\n' "$target" "$negative"
    done
done
