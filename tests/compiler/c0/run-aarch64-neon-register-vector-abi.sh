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

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_vector64_lanes.c" \
    -o "$work/vector64_lanes.i"
"$minic_cc" --target=aarch64-linux -S "$work/vector64_lanes.i" \
    -o "$work/vector64_lanes.s"
for lanes in 8b 4h 2s; do
    grep -F "  add v16.$lanes, v16.$lanes, v17.$lanes" \
        "$work/vector64_lanes.s" >/dev/null
done
grep -F '  add d16, d16, d17' "$work/vector64_lanes.s" >/dev/null
grep -F '  eor v16.8b, v16.8b, v17.8b' "$work/vector64_lanes.s" >/dev/null
grep -F '  blr x16' "$work/vector64_lanes.s" >/dev/null
grep -Eq '^  ldr d9, \[sp, #[0-9]+\]$' "$work/vector64_lanes.s"
if "$minic_cc" --target=x86_64-linux -S "$work/vector64_lanes.i" \
        -o "$work/vector64_lanes.x86.s" >/dev/null 2>&1; then
    printf '%s\n' 'FAIL AArch64 D-lane vector types leaked to x86' >&2
    exit 1
fi

grep -F '  bl add_d' "$work/vector64_basic.s" >/dev/null
grep -F '  bl add8' "$work/vector64_abi.s" >/dev/null
grep -F '  blr x16' "$work/vector64_abi.s" >/dev/null
grep -F '  bl ninth8' "$work/vector64_abi.s" >/dev/null
# Nine fixed SIMD arguments consume v0-v7 followed by a complete D slot in
# the caller's stack overflow area; the callee reloads its ninth argument.
grep -Eq '^  ldr d9, \[sp, #[0-9]+\]$' "$work/vector64_abi.s"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_float_lanes.c" \
    -o "$work/float_lanes.i"
"$minic_cc" --target=aarch64-linux -S "$work/float_lanes.i" \
    -o "$work/float_lanes.s"
grep -F '  fadd v16.2s, v16.2s, v17.2s' "$work/float_lanes.s" >/dev/null
grep -F '  fadd d16, d16, d17' "$work/float_lanes.s" >/dev/null
grep -F '  fadd v16.4s, v16.4s, v17.4s' "$work/float_lanes.s" >/dev/null
grep -F '  fadd v16.2d, v16.2d, v17.2d' "$work/float_lanes.s" >/dev/null
grep -F '  str d0,' "$work/float_lanes.s" >/dev/null
grep -F '  ldr d0,' "$work/float_lanes.s" >/dev/null
grep -F '  str q0,' "$work/float_lanes.s" >/dev/null
grep -F '  ldr q0,' "$work/float_lanes.s" >/dev/null
grep -F '  blr x16' "$work/float_lanes.s" >/dev/null
grep -Eq '^  ldr d9, \[sp, #[0-9]+\]$' "$work/float_lanes.s"
if "$minic_cc" --target=x86_64-linux -S "$work/float_lanes.i" \
        -o "$work/float_lanes.x86.s" >"$work/float_lanes.x86.stdout" \
        2>"$work/float_lanes.x86.stderr"; then
    printf '%s\n' 'FAIL AArch64 floating D/Q vector types leaked to x86' >&2
    exit 1
fi
for invalid in float_xor_invalid float16_opaque_invalid; do
    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/aarch64_neon_$invalid.c" \
        -o "$work/$invalid.i"
    if "$minic_cc" --target=aarch64-linux -S "$work/$invalid.i" \
            -o "$work/$invalid.s" >"$work/$invalid.stdout" \
            2>"$work/$invalid.stderr"; then
        printf '%s\n' "FAIL AArch64 invalid floating SIMD operation admitted: $invalid" >&2
        exit 1
    fi
done

printf '%s\n' 'PASS AArch64 integer D vector lane ADD/XOR and floating D/Q FADD by 2S/scalar-D/4S/2D, AAPCS64 SIMD v0/ninth/indirect ABI, x86 and invalid FP rejection; QEMU=NOT_TESTED'
