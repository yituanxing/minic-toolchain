#!/bin/sh
# Complete GNU↔MiniC differential for AAPCS64 mutable 32-byte va_list state.
set -eu
: "${MINIC_CC:?set MINIC_CC to the MiniC C driver}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_va_copy_abi.c"
"${CC:-cc}" -E -P -x c "$src" -o "$work/copy.i"
"$MINIC_CC" --target=aarch64-linux -S "$work/copy.i" -o "$work/minic.s"
grep -Fq 'ldp x11, x12, [x10]' "$work/minic.s"
grep -Fq 'stp x13, x14, [x9, #16]' "$work/minic.s"
grep -Fq 'add x9, sp, x9' "$work/minic.s"
gcc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
qemu=${QEMU_AARCH64:-qemu-aarch64}
if ! command -v "$gcc" >/dev/null 2>&1 ||
   ! command -v "$qemu" >/dev/null 2>&1; then
    echo 'AARCH64_VA_COPY_ABI=COMPILE_PASS GNU_QEMU=UNAVAILABLE'
    exit 0
fi
"$gcc" -static -x c "$src" -o "$work/gnu"
"$gcc" -static -x assembler "$work/minic.s" -o "$work/minic"
for compiler in gnu minic; do
    set +e
    timeout 45s "$qemu" "$work/$compiler"
    code=$?
    set -e
    if [ "$code" -ne 42 ]; then
        echo "AARCH64_VA_COPY_ABI=FAIL compiler=$compiler code=$code" >&2
        exit 1
    fi
done
echo 'AARCH64_VA_COPY_ABI=PASS GNU_QEMU=1 cursors=2 independent_32B=1 nonvariadic_copy=1'
