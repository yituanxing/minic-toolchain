#!/bin/sh
# Exact Linux AArch64 CASP/CASPAL + signed CASAL retry, with role negatives.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
src="$root/tests/compiler/c0/aarch64_linux_lse_atomic_full.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -std=gnu11 -E -P "$src" -o "$work/positive.i"
"$mini" --target=aarch64-linux -S "$work/positive.i" -o "$work/positive.s"
for mnemonic in casp caspal casal; do
    grep -Eq "^  $mnemonic " "$work/positive.s"
done
grep -Fq '  .arch_extension lse' "$work/positive.s"
grep -Fq '  cbnz x12, 1b' "$work/positive.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
         -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH64_LSE_FULL=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/register unsigned long x3 asm("x3")/register unsigned long x3 asm("x7")/' \
    "$src" >"$work/bad-fixed.c"
sed 's/\[old1\] "+&r"/\[old1\] "+r"/' \
    "$src" >"$work/bad-early.c"
sed 's/\[v\] "+Q"/\[v\] "+m"/g' \
    "$src" >"$work/bad-memory.c"
sed 's/"casp" ORDER/"caspX" ORDER/' \
    "$src" >"$work/bad-opcode.c"
sed 's/casal/cas/' \
    "$src" >"$work/bad-dec-order.c"
for variant in bad-fixed bad-early bad-memory bad-opcode bad-dec-order; do
    "${HOST_CC:-cc}" -std=gnu11 -E -P "$work/$variant.c" \
        -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
         -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "AARCH64_LSE_FULL=FAIL unsafe=$variant" >&2; exit 1
    fi
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -O2 -c "$src" -o "$work/gnu.o"
    echo 'AARCH64_LSE_FULL=PASS forms=3 foreign=2 negative=5 GAS=PASS'
else
    echo 'AARCH64_LSE_FULL=PASS forms=3 foreign=2 negative=5 GAS=SKIP'
fi
