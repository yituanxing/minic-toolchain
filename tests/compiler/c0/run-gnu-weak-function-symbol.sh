#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/gnu-weak-function-symbol"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

preprocess gnu_weak_function_symbol
"$minic" -S "$work/gnu_weak_function_symbol.i" -o "$work/gnu_weak_function_symbol.s"
for symbol in calibration_delay_done optional_hook later_weak weak_definition; do
    grep -F ".weak $symbol" "$work/gnu_weak_function_symbol.s" >/dev/null
done
grep -F ".globl strong_definition" "$work/gnu_weak_function_symbol.s" >/dev/null
if grep -F ".globl weak_definition" "$work/gnu_weak_function_symbol.s" >/dev/null; then
    echo "FAIL compiler/c0/gnu_weak_function_symbol: weak definition also emitted .globl" >&2
    exit 1
fi
for symbol in calibration_delay_done optional_hook later_weak; do
    if grep -F "$symbol:" "$work/gnu_weak_function_symbol.s" >/dev/null; then
        echo "FAIL compiler/c0/gnu_weak_function_symbol: declaration-only weak function emitted a body" >&2
        exit 1
    fi
done
grep -F "weak_definition:" "$work/gnu_weak_function_symbol.s" >/dev/null
grep -F "  call calibration_delay_done" "$work/gnu_weak_function_symbol.s" >/dev/null
grep -F "  call optional_hook" "$work/gnu_weak_function_symbol.s" >/dev/null
grep -F "  call later_weak" "$work/gnu_weak_function_symbol.s" >/dev/null

preprocess invalid_static_weak_function
if "$minic" -S "$work/invalid_static_weak_function.i" -o "$work/invalid_static_weak_function.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo "FAIL compiler/c0/invalid_static_weak_function: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "GNU weak requires external function linkage" "$work/invalid.stderr" >/dev/null

printf '%s\n' "PASS compiler/c0/gnu_weak_function_symbol registry=symbol binding=weak prefix+suffix=1 redeclaration=inherited declaration-only=.weak definition=.weak-not-globl strong=.globl static=reject"
