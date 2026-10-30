#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-weak-undefined-live
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_weak_undefined_live.c" -o "$work/input.i"
for target in x86_64 riscv32 riscv64 aarch64 loongarch64; do
    assembly="$work/$target.s"
    "$minic" --target="$target-linux" -S "$work/input.i" -o "$assembly"
    for symbol in missing_object hidden_missing_object provided_object \
                  missing_function hidden_missing_function provided_function; do
        test "$(grep -c "^\.weak $symbol$" "$assembly")" -eq 1
        if grep -q "^\.globl $symbol$" "$assembly"; then
            echo "FAIL duplicate strong/weak declaration: target=$target symbol=$symbol" >&2
            exit 1
        fi
    done
    for symbol in hidden_missing_object hidden_missing_function; do
        test "$(grep -c "^\.hidden $symbol$" "$assembly")" -eq 1
    done
    for symbol in unused_weak_object unused_weak_function; do
        if grep -q "^\.weak $symbol$" "$assembly"; then
            echo "FAIL dead weak declaration: target=$target symbol=$symbol" >&2
            exit 1
        fi
    done
done

"$host_cc" -fno-pie -c "$work/x86_64.s" -o "$work/minic.o"
"$host_cc" -fno-pie -c \
    "$root/tests/compiler/c0/gnu_weak_undefined_live.c" -o "$work/gnu.o"
"$host_cc" -fno-pie -c \
    "$root/tests/compiler/c0/gnu_weak_undefined_provider.c" -o "$work/provider.o"
"$host_cc" -no-pie "$work/gnu.o" "$work/provider.o" -o "$work/gnu"
"$host_cc" -no-pie "$work/minic.o" "$work/provider.o" -o "$work/minic"
"$work/gnu"
"$work/minic"

for symbol in missing_object hidden_missing_object missing_function hidden_missing_function; do
    readelf -Ws "$work/minic.o" | grep "WEAK.*UND $symbol$" >/dev/null
done
printf '%s\n' 'PASS GNU undefined weak ELF binding: five targets unique owner, live object+function, hidden, NULL, strong provider, dead declaration, x86 GNU AS/LD runtime'
