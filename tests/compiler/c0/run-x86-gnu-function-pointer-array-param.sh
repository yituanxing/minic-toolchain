#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-x86-fnptr-array-param
source="$root/tests/compiler/c0/x86_gnu_function_pointer_array_param.c"
driver="$root/tests/compiler/c0/x86_gnu_function_pointer_array_param_driver.c"
bad="$root/tests/compiler/c0/invalid_x86_gnu_function_pointer_array_param.c"

mkdir -p "$work"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -O2 -c "$source" -o "$work/gnu.o"
"$host_cc" -std=gnu11 -E -P "$source" -o "$work/input.i"
MINIC_CORE_IR=strict "$minic_cc" -S "$work/input.i" \
    --target=x86_64-linux -o "$work/minic.s"
"$host_cc" -c "$work/minic.s" -o "$work/minic.o"
for owner in gnu minic; do
    "$host_cc" -no-pie "$work/$owner.o" "$driver" -o "$work/$owner.elf"
    "$work/$owner.elf"
done

if "$host_cc" -std=gnu11 -Werror=incompatible-pointer-types -fsyntax-only "$bad" \
    >"$work/invalid.gnu.out" 2>"$work/invalid.gnu.err"; then
    echo 'FAIL GNU accepted incompatible function-pointer array argument' >&2
    exit 1
fi
"$host_cc" -std=gnu11 -E -P "$bad" -o "$work/invalid.i"
if "$minic_cc" -S "$work/invalid.i" --target=x86_64-linux \
    -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
    echo 'FAIL MiniC accepted incompatible function-pointer array argument' >&2
    exit 1
fi
grep -F 'call argument type does not match declaration' "$work/invalid.err" >/dev/null

printf '%s\n' 'PASS GNU/MiniC function-pointer array parameter decay: prototype+definition, dual ELF runtime=2, incompatible argument rejected by both'
