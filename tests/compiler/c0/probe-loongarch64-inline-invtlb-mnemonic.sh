#!/bin/sh
# The target instruction's opcode must not resurrect the dead generic
# always-inline C function, but a genuinely dynamic GNU `i` still fails.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/loong-inline-invtlb-mnemonic
mkdir -p "$work"
test -x "$minic"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/loongarch64_inline_invtlb_mnemonic.c" \
    -o "$work/source.i"
"$minic" --target=loongarch64-linux -mabi=lp64s -S \
    "$work/source.i" -o "$work/positive.s"
test "$(grep -Ec '^[[:space:]]*invtlb[[:space:]]+0,' "$work/positive.s")" -eq 1

# A used dynamic immediate operand is not made legal by mnemonic filtering.
sed 's/invtlb(0U, info, address);/invtlb(info, info, address);/' \
    "$work/source.i" >"$work/invalid.i"
if cmp -s "$work/source.i" "$work/invalid.i"; then
    echo 'LOONG_INVTLB_MNEMONIC=FAIL mutation_missing=1' >&2
    exit 1
fi
rm -f "$work/invalid.s"
if "$minic" --target=loongarch64-linux -mabi=lp64s -S \
    "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.out" 2>"$work/invalid.err"; then
    echo 'LOONG_INVTLB_MNEMONIC=FAIL admitted_dynamic_i=1' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'LOONG_INVTLB_MNEMONIC=PASS constant_inline=1 dynamic_i_rejected=1 GNU_AS=NOT_RUN'
