#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/aarch64-neon-register-abi
mkdir -p "$work"

for probe in vector64_basic vector64_abi; do
    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/aarch64_neon_$probe.c" -o "$work/$probe.i"
    "$minic_cc" --target=aarch64-linux -S "$work/$probe.i" \
        -o "$work/$probe.s"
    grep -F '  str d0,' "$work/$probe.s" >/dev/null
    grep -F '  ldr d0,' "$work/$probe.s" >/dev/null
    grep -F '  add v16.8b, v16.8b, v17.8b' "$work/$probe.s" >/dev/null
    grep -F '  eor v16.8b, v16.8b, v17.8b' "$work/$probe.s" >/dev/null
    if "$minic_cc" --target=x86_64-linux -S "$work/$probe.i" \
            -o "$work/$probe.x86.s" >"$work/$probe.x86.stdout" \
            2>"$work/$probe.x86.stderr"; then
        printf '%s\n' "FAIL AdvSIMD D-vector syntax leaked to x86: $probe" >&2
        exit 1
    fi
done

grep -F '  bl add_d' "$work/vector64_basic.s" >/dev/null
grep -F '  bl add8' "$work/vector64_abi.s" >/dev/null
grep -F '  blr x16' "$work/vector64_abi.s" >/dev/null
grep -F '  bl ninth8' "$work/vector64_abi.s" >/dev/null
# Nine fixed SIMD arguments consume v0-v7 followed by a complete D slot in
# the caller's stack overflow area; the callee reloads its ninth argument.
grep -Eq '^  ldr d9, \[sp, #[0-9]+\]$' "$work/vector64_abi.s"

printf '%s\n' 'PASS AArch64 64-bit D-vector add/xor, direct and indirect typed v0 return, eight FPR args plus stack overflow, x86 rejection; QEMU=NOT_TESTED'
