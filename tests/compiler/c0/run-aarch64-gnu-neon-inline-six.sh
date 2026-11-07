#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-gcc}
work=${BUILD_DIR:-"$root/build/release"}/tests/aarch64-gnu-neon-inline-six
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/aarch64_gnu_neon_inline_six.c" \
    -o "$work/six.i"
"$cc" --target=aarch64-linux -S "$work/six.i" -o "$work/six.s"

if [ "$(grep -Ec '^[[:space:]]+eor v[0-9]+\.16b,' "$work/six.s")" -ne 2 ] ||
   ! grep -Eq '^[[:space:]]+ldr q[0-9]+, \[x[0-9]+' "$work/six.s" ||
   ! grep -Eq '^[[:space:]]+str q[0-9]+, \[x[0-9]+' "$work/six.s" ||
   [ "$(grep -Ec '^[[:space:]]+bl next8$' "$work/six.s")" -ne 1 ] ||
   grep -Eq '^[[:space:]]+bl (vld1q_|vst1q_|veorq_|__minic_target_builtin_)' "$work/six.s"; then
    printf '%s\n' 'FAIL AArch64 GNU NEON wrappers: memory/eor/once/or physical BL evidence' >&2
    exit 1
fi

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_gnu_neon_inline_header_signature.c" \
    -o "$work/header.i"
"$cc" --target=aarch64-linux -S "$work/header.i" -o "$work/header.s"

for test_name in bad_signature bad_cfg bad_spoof bad_reduction \
    bad_pointer_qi_di bad_pointer_di_qi bad_pointer_di_rank; do
    source_file="$root/tests/compiler/c0/aarch64_gnu_neon_inline_$test_name.c"
    "$host_cc" -E -P -x c "$source_file" -o "$work/$test_name.i"
    if "$cc" --target=aarch64-linux -S "$work/$test_name.i" \
        -o "$work/$test_name.s" >"$work/$test_name.stdout" 2>"$work/$test_name.stderr"; then
        printf '%s\n' "FAIL AArch64 GNU NEON negative=$test_name" >&2
        exit 1
    fi
done

if "$cc" --target=x86_64-linux -S "$work/six.i" -o "$work/foreign.s" \
   >"$work/foreign.stdout" 2>"$work/foreign.stderr"; then
    printf '%s\n' 'FAIL AArch64 GNU NEON builtin leaked to x86' >&2
    exit 1
fi

printf '%s\n' \
    'PASS AArch64 six GNU NEON inline wrappers source/native: ldr/str q + eor + dynamic-once + strict negatives; GNU13/QEMU Stage3 remains separate'
