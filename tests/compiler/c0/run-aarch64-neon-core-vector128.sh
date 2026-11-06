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
# The typed AArch64 addressing serializer canonicalizes register-indirect
# `[x10]` as `[x10, #0]`; both are the identical zero-offset Q memory
# access. Keep the exact vector register, base, and zero displacement sealed.
grep -Eq '^[[:space:]]*ldr[[:space:]]+q16,[[:space:]]*\[x10(,[[:space:]]*#0)?\][[:space:]]*$' \
    "$work/vector.s"
grep -Eq '^[[:space:]]*str[[:space:]]+q16,[[:space:]]*\[x10(,[[:space:]]*#0)?\][[:space:]]*$' \
    "$work/vector.s"

# Both admitted lane layouts must lower ADD to native, correctly sized lanes.
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_vector128_add_core.c" \
    -o "$work/add.i"
"$minic_cc" --target=aarch64-linux -S "$work/add.i" \
    -o "$work/add.s"
grep -F '  add v16.16b, v16.16b, v17.16b' "$work/add.s" >/dev/null
grep -F '  add v16.2d, v16.2d, v17.2d' "$work/add.s" >/dev/null

# The original Stage2 const vector-pointer ALU probe is now a positive
# AArch64 Core/V-register case; it never passes a vector across the C ABI.
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_unsupported_alu.c" \
    -o "$work/const-alu.i"
"$minic_cc" --target=aarch64-linux -S "$work/const-alu.i" \
    -o "$work/const-alu.s"
grep -F '  eor v16.16b, v16.16b, v17.16b' "$work/const-alu.s" >/dev/null

# The target registry, not generic C Frontend or Core IR, owns the complete
# GNU/AAPCS64 short-vector spelling inventory. D byte-lane vectors are now
# modeled; remaining unsupported lane families stay opaque and fail closed.
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_named_header_types.c" \
    -o "$work/header-types.i"
"$minic_cc" --target=aarch64-linux -S "$work/header-types.i" \
    -o "$work/header-types.a64.s"
if "$minic_cc" --target=x86_64-linux -S "$work/header-types.i" \
    -o "$work/header-types.x86.s" >"$work/header-types.x86.stdout" \
    2>"$work/header-types.x86.stderr"; then
    printf '%s\n' 'FAIL AArch64 ACLE header typedef leaked to x86' >&2
    exit 1
fi
grep -F 'expected type name' "$work/header-types.x86.stderr" >/dev/null
printf '%s\n' 'typedef __Int16x4_t a64_opaque_d_vector;' \
    'int main(void) { a64_opaque_d_vector unsupported_object; return (int)sizeof(unsupported_object); }' \
    >"$work/header-types-unsupported.i"
rm -f "$work/header-types-unsupported.s"
if "$minic_cc" --target=aarch64-linux -S "$work/header-types-unsupported.i" \
    -o "$work/header-types-unsupported.s" >"$work/header-types-unsupported.stdout" \
    2>"$work/header-types-unsupported.stderr"; then
    printf '%s\n' 'FAIL unmodeled AArch64 D-vector acquired object storage' >&2
    exit 1
fi
test ! -e "$work/header-types-unsupported.s"

# AArch64 builtin source identities are not portable to another backend.
if "$minic_cc" --target=x86_64-linux -S "$work/vector.i" \
    -o "$work/vector.x86.s" >"$work/vector.x86.stdout" \
    2>"$work/vector.x86.stderr"; then
    printf '%s\n' 'FAIL AArch64 vector identity admitted on x86' >&2
    exit 1
fi
grep -F 'expected type name' "$work/vector.x86.stderr" >/dev/null

for bad in vector128_unsupported_add invalid_mixed_xor; do
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

# AAPCS64 Q-vector identity is a typed FPR-bank call, never an integer-pair
# surrogate. This former negative becomes a positive ABI emission assertion.
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_unsupported_abi.c" \
    -o "$work/typed-q-abi.i"
"$minic_cc" --target=aarch64-linux -S "$work/typed-q-abi.i" \
    -o "$work/typed-q-abi.s"
grep -F '  str q0,' "$work/typed-q-abi.s" >/dev/null
grep -F '  ldr q0,' "$work/typed-q-abi.s" >/dev/null

printf '%s\n' 'PASS AArch64 typed Q-vector eor/add(16B,2D), load/store, Q ABI, foreign ISA reject, unsupported mixed-lane ADD/XOR; QEMU runtime=NOT_TESTED'
