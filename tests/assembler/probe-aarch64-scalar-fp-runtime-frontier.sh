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

# Same named instructions must not accept illegal register-bank or NZCV
# combinations and accidentally encode nonsense.
for invalid in \
    'fabd d0, s0, d8' \
    'fccmp d0, s0, #0, ne' \
    'fccmp d0, d0, #16, ne' \
    'fcvtzs d0, s0' \
    'fcvtzs x1, x0' \
    'movi s0, #0'; do
    printf '%s\n' '.text' "$invalid" >"$work/invalid.s"
    if "$minias" --target=aarch64 -march=armv8-a -mabi=lp64 \
        -o "$work/invalid.o" "$work/invalid.s" \
        >"$work/negative.log" 2>&1; then
        echo "AARCH64_SCALAR_FP_FRONTIER=FAIL accepted $invalid" >&2
        exit 1
    fi
done

echo 'AARCH64_SCALAR_FP_FRONTIER=PASS real_cjson_lua_forms=7 invalid=6'
