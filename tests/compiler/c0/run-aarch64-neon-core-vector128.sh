#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/aarch64-neon-core-vector128
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_vector128_xor_core.c" \
    -o "$work/vector.i"
"$minic_cc" --target=aarch64-linux -S "$work/vector.i" \
    -o "$work/vector.s"

# Two independent typed vector expressions, u64 lanes and byte lanes.
# No scalar OR, pairwise GPR surrogate or record byte-copy is accepted here.
count=$(grep -Fc '  eor v16.16b, v16.16b, v17.16b' "$work/vector.s" || true)
if [ "$count" -ne 2 ]; then
    printf '%s\n' "FAIL expected 2 native AArch64 Q-vector eor instructions, got $count" >&2
    exit 1
fi
grep -F '  ldr q16, [x10]' "$work/vector.s" >/dev/null
grep -F '  str q16, [x10]' "$work/vector.s" >/dev/null

# The original Stage2 const vector-pointer ALU probe is now a positive
# AArch64 Core/V-register case; it never passes a vector across the C ABI.
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_unsupported_alu.c" \
    -o "$work/const-alu.i"
"$minic_cc" --target=aarch64-linux -S "$work/const-alu.i" \
    -o "$work/const-alu.s"
grep -F '  eor v16.16b, v16.16b, v17.16b' "$work/const-alu.s" >/dev/null

# AArch64 builtin source identities are not portable to another backend.
if "$minic_cc" --target=x86_64-linux -S "$work/vector.i" \
    -o "$work/vector.x86.s" >"$work/vector.x86.stdout" \
    2>"$work/vector.x86.stderr"; then
    printf '%s\n' 'FAIL AArch64 vector identity admitted on x86' >&2
    exit 1
fi
grep -F 'expected type name' "$work/vector.x86.stderr" >/dev/null

for bad in vector128_unsupported_add unsupported_abi invalid_mixed_xor; do
    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/aarch64_neon_$bad.c" \
        -o "$work/$bad.i"
    if "$minic_cc" --target=aarch64-linux -S "$work/$bad.i" \
        -o "$work/$bad.s" >"$work/$bad.stdout" \
        2>"$work/$bad.stderr"; then
        printf '%s\n' "FAIL unfinished vector operation unexpectedly admitted: $bad" >&2
        exit 1
    fi
done

printf '%s\n' 'PASS AArch64 typed vector128 Core->Q eor x2 + const-pointer ALU, load/store, foreign ISA reject, unsupported add/ABI/mixed-lane reject; QEMU runtime=NOT_TESTED'
