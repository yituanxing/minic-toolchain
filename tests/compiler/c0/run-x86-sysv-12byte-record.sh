#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-x86-sysv-12byte-record
mkdir -p "$work"

source_file="$root/tests/compiler/c0/x86_sysv_12byte_record.c"
driver_file="$root/tests/compiler/c0/x86_sysv_12byte_record_driver.c"

"$cc" -std=c11 -Wall -Wextra -Werror -c "$driver_file" -o "$work/driver.o"
"$cc" -std=c11 -Wall -Wextra -Werror -c "$source_file" -o "$work/gnu.o"
"$cc" "$work/gnu.o" "$work/driver.o" -o "$work/gnu-control"
"$work/gnu-control"

"$cc" -E -P "$source_file" -o "$work/minic.i"
"$minic" --target=x86_64-linux -S "$work/minic.i" -o "$work/minic.s"
"$cc" -c "$work/minic.s" -o "$work/minic.o"
"$cc" "$work/minic.o" "$work/driver.o" -o "$work/gnu-minic-abi"
"$work/gnu-minic-abi"

invalid="$root/tests/compiler/c0/invalid_x86_sysv_12byte_mixed_record.c"
"$cc" -E -P "$invalid" -o "$work/invalid.i"
if "$minic" --target=x86_64-linux -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL x86 SysV 12B mixed SSE/INTEGER record was incorrectly admitted' >&2
    exit 1
fi
if ! grep -E 'x86-64 (cannot allocate or verify Core function frame|Core terminator unsupported)' \
    "$work/invalid.stderr" >/dev/null; then
    printf '%s\n' 'FAIL x86 SysV 12B mixed record missing semantic ABI rejection' >&2
    sed -n '1,30p' "$work/invalid.stderr" >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0 x86 SysV 12-byte INTEGER record GNU-control+GNU/MiniC-cross-ABI+negative-mixed'
