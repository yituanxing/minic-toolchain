#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
src="$root/tests/compiler/c0/aarch64_linux_mte_sdei_mrs.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 "$src" -o "$work/positive.i"
"$mini" --target=aarch64-linux -S "$work/positive.i" -o "$work/positive.s"
test "$(grep -Ec '^  mrs x9, ' "$work/positive.s")" -eq 2
grep -Fq '  mrs x9, CNTVCT_EL0' "$work/positive.s"
grep -Fq '  mrs x9, vbar_el1' "$work/positive.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "MTE_SDEI_MRS=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/READ_REG(vbar_el1)/READ_REG(vbar_el7)/' "$src" >"$work/bad-reg.c"
sed 's/"=r" (value)/"=m" (value)/g' "$src" >"$work/bad-constraint.c"
sed 's/unsigned long long/unsigned int/g' "$src" >"$work/bad-width.c"
for variant in bad-reg bad-constraint bad-width; do
    "${HOST_CC:-cc}" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "MTE_SDEI_MRS=FAIL unsafe=$variant" >&2; exit 1
    fi
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'MTE_SDEI_MRS=PASS registers=2 foreign=2 negative=3 GAS=PASS'
else
    echo 'MTE_SDEI_MRS=PASS registers=2 foreign=2 negative=3 GAS=SKIP'
fi
