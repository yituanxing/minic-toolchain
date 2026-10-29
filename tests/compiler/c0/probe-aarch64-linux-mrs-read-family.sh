#!/bin/sh
# Original privileged MRS contracts: named and finite architectural banks.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_mrs_read_family.c"
"${HOST_CC:-cc}" -E -P -std=gnu11 "$src" -o "$work/read.i"
"$mini" --target=aarch64-linux -S "$work/read.i" -o "$work/read.s"
test "$(grep -Ec '^  mrs x9, ' "$work/read.s")" -eq 31
for reg in pmcr_el0 pmceid0_el0 pmceid1_el0 cntpct_el0 \
    cnthctl_el2 SCTLR_EL1 dbgbcr15_el1 dbgbvr0_el1 \
    pmevcntr30_el0 pmevtyper30_el0 s3_0_c15_c0_5; do
    grep -Fq "  mrs x9, $reg" "$work/read.s"
done
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/read.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH64_MRS_READ=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/READ_SYSREG(pmcr_el0)/READ_SYSREG(pmcr_el7)/' \
    "$src" >"$work/bad-reg.c"
sed 's/"=r" (result)/"=m" (result)/g' "$src" >"$work/bad-constraint.c"
sed 's/unsigned long long/unsigned int/g' "$src" >"$work/bad-width.c"
for variant in bad-reg bad-constraint bad-width; do
    "${HOST_CC:-cc}" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "AARCH64_MRS_READ=FAIL unsafe-$variant" >&2; exit 1
    fi
    test ! -e "$work/$variant.s"
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/read.s" -o "$work/mini.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'AARCH64_MRS_READ=PASS originals=31 foreign=2 negative=3 GNU_GAS=PASS'
else
    echo 'AARCH64_MRS_READ=PASS originals=31 foreign=2 negative=3 GNU_GAS=SKIP'
fi
