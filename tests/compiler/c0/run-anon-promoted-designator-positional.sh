#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-anon-promoted-designator-positional

mkdir -p "$work"
"$host_cc" -std=gnu11 -Wall -Wextra -fsyntax-only \
    "$root/tests/compiler/c0/anon_promoted_designator_positional.c"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/anon_promoted_designator_positional.c" -o "$work/good.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S \
    "$work/good.i" -o "$work/x86.s"
"$host_cc" -no-pie "$work/x86.s" -o "$work/x86" 2>"$work/assembly-warnings.txt"
"$work/x86"
"$host_cc" -std=gnu11 "$root/tests/compiler/c0/anon_promoted_designator_positional.c" \
    -o "$work/gnu"
"$work/gnu"

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/good.i" -o "$work/$target.s"
done

if "$host_cc" -std=gnu11 -pedantic-errors -fsyntax-only \
    "$root/tests/compiler/c0/invalid_nested_designator_positional_overflow.c" \
    >"$work/negative.gnu.stdout" 2>"$work/negative.gnu.stderr"; then
    echo 'FAIL GCC accepted nested positional initializer overflow' >&2
    exit 1
fi
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_nested_designator_positional_overflow.c" \
    -o "$work/negative.i"
if "$minic_cc" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" \
    >"$work/negative.stdout" 2>"$work/negative.stderr"; then
    echo 'FAIL MiniC accepted nested positional initializer overflow' >&2
    exit 1
fi
grep -F 'too many nested static record initializers' "$work/negative.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/anon-promoted-designator-positional GNU-nested=1 anonymous-union=1 bitfield=1 scalar-followup=1 overflow-rejected=1 five-targets=1 x86-native=1 strict-core=1'
