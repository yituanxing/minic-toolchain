#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
src="$root/tests/compiler/c0/aarch64_linux_pmu_pmccfiltr_read.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 "$src" -o "$work/positive.i"
"$mini" --target=aarch64-linux -fshort-wchar -funsigned-char -fno-PIE \
    -S "$work/positive.i" -o "$work/positive.s"
test "$(grep -Fc '  mrs x9, pmccfiltr_el0' "$work/positive.s")" -eq 1
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "PMCCFILTR=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/pmccfiltr_el0/pmccfiltr_el7/g' "$src" >"$work/bad-reg.c"
sed 's/"=r" (value)/"=m" (value)/' "$src" >"$work/bad-constraint.c"
sed 's/unsigned long long/unsigned int/g' "$src" >"$work/bad-width.c"
for variant in bad-reg bad-constraint bad-width; do
    "${HOST_CC:-cc}" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "PMCCFILTR=FAIL unsafe=$variant" >&2; exit 1
    fi
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'PMCCFILTR=PASS register=1 foreign=2 negative=3 GAS=PASS'
else
    echo 'PMCCFILTR=PASS register=1 foreign=2 negative=3 GAS=SKIP'
fi
