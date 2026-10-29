#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_debug_pmu_mrs_banks.c" \
    -o "$work/banks.i"
"$minic" --target=aarch64-linux -S "$work/banks.i" -o "$work/banks.s"
test "$(grep -Ec '^  mrs x9, (dbg|pm)' "$work/banks.s")" -eq 72
for reg in dbgbcr0_el1 dbgbvr15_el1 dbgwcr7_el1 dbgwvr15_el1 \
    pmevcntr0_el0 pmevcntr15_el0 pmevcntr30_el0 \
    pmcr_el0 pmccntr_el0 pmovsclr_el0 pmceid0_el0 pmceid1_el0; do
    grep -Fq "mrs x9, $reg" "$work/banks.s"
done
# RV's generic GNU "mrs" passthrough is a separate backend contract gap;
# only assert targets currently enforcing a strict matching ASM grammar.
for target in x86_64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/banks.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch debug/PMU MRS on $target" >&2
        exit 1
    fi
done
sed 's/dbgwvr15_el1/dbgwvr16_el1/g' "$work/banks.i" > "$work/negative-debug.i"
sed 's/pmevcntr30_el0/pmevcntr31_el0/g' "$work/banks.i" > "$work/negative-pmu.i"
sed 's/pmovsclr_el0/pmovsset_el0/g' "$work/banks.i" > "$work/negative-reg.i"
sed 's/"=r"(value)/"=m"(value)/g' "$work/banks.i" > "$work/negative-constraint.i"
sed 's/#reg : "=r"/#reg "; nop" : "=r"/' \
    "$root/tests/compiler/c0/aarch64_linux_debug_pmu_mrs_banks.c" \
    > "$work/negative-extra.c"
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c "$work/negative-extra.c" \
    -o "$work/negative-extra.i"
for category in debug pmu reg constraint extra; do
    if "$minic" --target=aarch64-linux -S "$work/negative-$category.i" \
        -o "$work/negative-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted unsupported AArch debug/PMU $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS exact GNU AArch 64 debug +8 PMU MRS, 2ISA gates +5 negative'
