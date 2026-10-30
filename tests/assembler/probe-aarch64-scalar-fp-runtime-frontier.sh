#!/usr/bin/env bash
# Instruction bytes taken from GNU binutils AArch64 opcode table.  These are
# the scalar FP forms emitted by GCC while compiling real cJSON and Lua units.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

printf '%s\n' '.text' \
    'fabd d0, d0, d8' \
    'fccmp d0, d0, #0, ne' \
    'fccmpe s1, s2, #15, eq' \
    'movi d0, #0' \
    'fcvtzs d0, d0' \
    'fcvtzs x1, d0' \
    'fcvtzu x2, d0' \
    'ret' >"$work/frontier.s"
"$minias" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/frontier.o" "$work/frontier.s"
readelf -x .text "$work/frontier.o" >"$work/actual.hex"
# 7ee8d400 1e601400 1e22043f 2f00e400
# 5ee1b800 9e780001 9e790002 d65f03c0, all little-endian.
for expected in \
    '00d4e87e 0014601e 3f04221e 00e4002f' \
    '00b8e15e 0100789e 0200799e c0035fd6'; do
    if ! grep -qi "$expected" "$work/actual.hex"; then
        echo "AARCH64_SCALAR_FP_FRONTIER=FAIL expected=$expected" >&2
        sed -n '1,12p' "$work/actual.hex" >&2
        exit 1
    fi
done

# Lua's lmathlib emits an AdvSIMD *scalar D* shift, not a Vn.2D shift.
# Cover the complete signed/unsigned/insert/accumulate immediate-shift set
# sharing this encoding template. The two vector checks defend Q handling.
printf '%s\n' '.text' \
    'ushr d0, d0, #11' \
    'sshr d0, d0, #11' \
    'sri d0, d0, #11' \
    'shl d0, d0, #11' \
    'sli d0, d0, #11' \
    'usra d0, d0, #11' \
    'ssra d0, d0, #11' \
    'urshr d0, d0, #11' \
    'srshr d0, d0, #11' \
    'ursra d0, d0, #11' \
    'srsra d0, d0, #11' \
    'ushr v0.2d, v1.2d, #11' \
    'shl v2.8b, v3.8b, #1' \
    'ret' >"$work/scalar-shifts.s"
"$minias" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/scalar-shifts.o" "$work/scalar-shifts.s"
readelf -x .text "$work/scalar-shifts.o" >"$work/shifts.hex"
for expected in \
    '0004757f 0004755f 0044757f 00544b5f' \
    '00544b7f 0014757f 0014755f 0024757f' \
    '0024755f 0034757f 0034755f 2004756f' \
    '6254090f c0035fd6'; do
    if ! grep -qi "$expected" "$work/shifts.hex"; then
        echo "AARCH64_SCALAR_SHIFT_FRONTIER=FAIL expected=$expected" >&2
        sed -n '1,14p' "$work/shifts.hex" >&2
        exit 1
    fi
done

# GNU GAS intentionally lowers unaligned/negative LDR/STR to LDUR/STUR;
# pre/post indexing is signed-9-bit as well, including SIMD scalar stores.
printf '%s\n' '.text' \
    'ldr s0,[x1,#3]' \
    'str d0,[x1,#-8]' \
    'str s0,[x1,#16]!' \
    'ldr d0,[x1],#24' \
    'str q0,[x1],#-16' \
    'ret' >"$work/fp-memory.s"
"$minias" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/fp-memory.o" "$work/fp-memory.s"
readelf -x .text "$work/fp-memory.o" >"$work/fp-memory.hex"
for expected in \
    '203040bc 20801ffc 200c01bc 208441fc' \
    '20049f3c c0035fd6'; do
    if ! grep -qi "$expected" "$work/fp-memory.hex"; then
        echo "AARCH64_FP_MEMORY_FRONTIER=FAIL expected=$expected" >&2
        sed -n '1,12p' "$work/fp-memory.hex" >&2
        exit 1
    fi
done

# Same named instructions must not accept illegal register-bank or NZCV
# combinations and accidentally encode nonsense.
for invalid in \
    'fabd d0, s0, d8' \
    'fccmp d0, s0, #0, ne' \
    'fccmp d0, d0, #16, ne' \
    'fcvtzs d0, s0' \
    'fcvtzs x1, x0' \
    'movi s0, #0' \
    'ushr d0, d1, #0' \
    'ushr d0, d1, #65' \
    'shl d0, d1, #64' \
    'ushr d0, s1, #11' \
    'sli s0, s1, #11' \
    'sri v0.1d, v1.1d, #11' \
    'ldr s0,[x1,#16384]' \
    'str d0,[x1,#-257]' \
    'str s0,[x1,#256]!'; do
    printf '%s\n' '.text' "$invalid" >"$work/invalid.s"
    if "$minias" --target=aarch64 -march=armv8-a -mabi=lp64 \
        -o "$work/invalid.o" "$work/invalid.s" \
        >"$work/negative.log" 2>&1; then
        echo "AARCH64_SCALAR_FP_FRONTIER=FAIL accepted $invalid" >&2
        exit 1
    fi
done

echo 'AARCH64_SCALAR_FP_FRONTIER=PASS real_cjson_lua_forms=7 scalar_shifts=11 vector_shifts=2 fp_memory=5 invalid=15'
