#!/bin/sh
# Source/ELF oracle for a mechanical RV64 GlobalEmissionPlan migration.
# The host preprocesses each C file ONCE; an optional C38 MiniC baseline
# consumes exactly the same .i, so changed GNU spelling is caught bytewise.
# Full GNU as/ld/readelf/QEMU evidence is opt-in and fails closed if absent.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
baseline=${MINIC_RV64_GLOBAL_BASELINE_CC:-}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/rv64-global-plan-abi
positive="$root/tests/compiler/c0/rv64_global_plan_abi.c"
bad_section="$root/tests/compiler/c0/invalid_rv64_global_plan_section_injection.c"
bad_bss="$root/tests/compiler/c0/invalid_rv64_global_plan_bss_initializer.c"
mkdir -p "$work"

test -x "$minic" || { echo "RV64_GLOBAL_PLAN_SOURCE=BLOCKED missing MiniC $minic" >&2; exit 2; }
if test -n "$baseline"; then
    test -x "$baseline" || {
        echo "RV64_GLOBAL_PLAN_DIFF=BLOCKED missing baseline $baseline" >&2
        exit 2
    }
fi

"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$positive"
"$host_cc" -std=gnu11 -E -P -x c "$positive" -o "$work/positive.i"
"$host_cc" -E -P -x c "$bad_section" -o "$work/bad-section.i"
"$host_cc" -E -P -x c "$bad_bss" -o "$work/bad-bss.i"

"$minic" --target=riscv64-linux -S "$work/positive.i" -o "$work/mini.s"
test -s "$work/mini.s"
for symbol in rv64_plan_anchor rv64_plan_byte rv64_plan_float \
              rv64_plan_double rv64_plan_pointer rv64_plan_zero_tail \
              rv64_plan_tentative rv64_plan_named_bss rv64_plan_named_data \
              rv64_plan_nested rv64_plan_pointer_grid rv64_plan_flexible \
              rv64_plan_bitfield; do
    grep -Fq "$symbol:" "$work/mini.s"
    grep -Fq ".size $symbol," "$work/mini.s"
done
grep -Fq '  .dword rv64_plan_anchor' "$work/mini.s"
grep -Fq '  .dword rv64_plan_add' "$work/mini.s"
grep -Fq '.section .bss.minic.rv64.plan' "$work/mini.s"
grep -Fq '.section .bssish.minic.rv64.plan' "$work/mini.s"

# Section-name injection must never be printed into a valid GAS output.
rm -f "$work/mini-bad-section.s"
if "$minic" --target=riscv64-linux -S "$work/bad-section.i" \
    -o "$work/mini-bad-section.s" >"$work/bad-section.out" \
    2>"$work/bad-section.err"; then
    echo 'RV64_GLOBAL_PLAN_SOURCE=FAIL unsafe ELF section accepted' >&2
    exit 1
fi
test ! -e "$work/mini-bad-section.s"

# Deliberate historical admission debt: RV64 accepts a nonzero initializer
# in a custom .bss.* section that GNU AS treats as NOBITS. Preserve the C38
# accepted/rejected decision and .s when doing equivalence review; do not
# count that behavior as GNU ABI correctness or quietly strengthen this gate.
mini_bss=reject
if "$minic" --target=riscv64-linux -S "$work/bad-bss.i" \
    -o "$work/mini-bad-bss.s" >"$work/mini-bad-bss.out" \
    2>"$work/mini-bad-bss.err"; then
    mini_bss=accept
fi

if test -n "$baseline"; then
    "$baseline" --target=riscv64-linux -S "$work/positive.i" \
        -o "$work/baseline.s"
    if ! cmp -s "$work/baseline.s" "$work/mini.s"; then
        echo 'RV64_GLOBAL_PLAN_DIFF=FAIL baseline/new positive .s differs' >&2
        diff -u "$work/baseline.s" "$work/mini.s" || true
        exit 1
    fi
    rm -f "$work/baseline-bad-section.s"
    if "$baseline" --target=riscv64-linux -S "$work/bad-section.i" \
        -o "$work/baseline-bad-section.s" >"$work/baseline-bad-section.out" \
        2>"$work/baseline-bad-section.err"; then
        echo 'RV64_GLOBAL_PLAN_DIFF=FAIL baseline admitted section injection' >&2
        exit 1
    fi
    test ! -e "$work/baseline-bad-section.s"
    baseline_bss=reject
    if "$baseline" --target=riscv64-linux -S "$work/bad-bss.i" \
        -o "$work/baseline-bad-bss.s" >"$work/baseline-bad-bss.out" \
        2>"$work/baseline-bad-bss.err"; then
        baseline_bss=accept
    fi
    if test "$baseline_bss" != "$mini_bss"; then
        echo 'RV64_GLOBAL_PLAN_DIFF=FAIL changed historical .bss admission' >&2
        exit 1
    fi
    if test "$mini_bss" = accept &&
       ! cmp -s "$work/baseline-bad-bss.s" "$work/mini-bad-bss.s"; then
        echo 'RV64_GLOBAL_PLAN_DIFF=FAIL changed accepted .bss .s bytes' >&2
        exit 1
    fi
    echo "RV64_GLOBAL_PLAN_DIFF=PASS identical_positive_s=1 section_injection_rejected=1 bss_admission=$mini_bss"
fi

echo "RV64_GLOBAL_PLAN_SOURCE=PASS positive=13 nested_reloc=1 flexible=1 bitfield=1 section_injection_rejected=1 historical_bss_admission=$mini_bss GNU_ELF=NOT_RUN"

if test "${MINIC_RV64_GLOBAL_GNU_ELF:-0}" != 1; then
    exit 0
fi

cross_gcc=${RV64_GNU_GCC:-riscv64-linux-gnu-gcc}
cross_as=${RV64_GNU_AS:-riscv64-linux-gnu-as}
cross_ld=${RV64_GNU_LD:-riscv64-linux-gnu-ld}
cross_readelf=${RV64_GNU_READELF:-riscv64-linux-gnu-readelf}
qemu=${RV64_QEMU:-qemu-riscv64}
for required in "$cross_gcc" "$cross_as" "$cross_ld" "$cross_readelf" "$qemu"; do
    if ! command -v "$required" >/dev/null 2>&1; then
        echo "RV64_GLOBAL_PLAN_GNU_ELF=BLOCKED missing=$required (not PASS)" >&2
        exit 2
    fi
done

"$cross_gcc" -O0 -fno-pie -c -x c "$work/positive.i" -o "$work/gnu.o"
"$cross_as" "$work/mini.s" -o "$work/mini.o"
"$cross_ld" -r "$work/mini.o" -o "$work/mini-reloc-linked.o"
for owner in gnu mini; do
    "$cross_readelf" -sW "$work/$owner.o" >"$work/$owner.symbols"
    "$cross_readelf" -rW "$work/$owner.o" >"$work/$owner.relocs"
    "$cross_readelf" -SW "$work/$owner.o" >"$work/$owner.sections"
done
"$cross_readelf" -rW "$work/mini-reloc-linked.o" >"$work/mini-linked.relocs"
grep -Fq 'R_RISCV_64' "$work/mini.relocs"
grep -Fq 'R_RISCV_64' "$work/mini-linked.relocs"
for symbol in rv64_plan_anchor rv64_plan_byte rv64_plan_float \
              rv64_plan_double rv64_plan_pointer rv64_plan_zero_tail \
              rv64_plan_tentative rv64_plan_named_bss rv64_plan_named_data \
              rv64_plan_nested rv64_plan_pointer_grid rv64_plan_flexible \
              rv64_plan_bitfield; do
    gnu_size=$(awk -v name="$symbol" '$NF == name { print $3; exit }' "$work/gnu.symbols")
    mini_size=$(awk -v name="$symbol" '$NF == name { print $3; exit }' "$work/mini.symbols")
    if test -z "$gnu_size" || test "$gnu_size" != "$mini_size"; then
        echo "RV64_GLOBAL_PLAN_GNU_ELF=FAIL symbol size $symbol GNU=$gnu_size MiniC=$mini_size" >&2
        exit 1
    fi
done

# GNU NOBITS vs RV64's pre-migration PROGBITS for custom .bss.* is a
# separately disclosed known divergence, not a claim of full section parity.
gnu_bss=$(awk '$3 == ".bss.minic.rv64.plan" { print $4; exit }' "$work/gnu.sections")
mini_bss_type=$(awk '$3 == ".bss.minic.rv64.plan" { print $4; exit }' "$work/mini.sections")
echo "RV64_GLOBAL_PLAN_BSS_PARITY=OBSERVED GNU=$gnu_bss MiniC=$mini_bss_type (not a parity PASS)"
"$cross_gcc" -O0 -fno-pie -no-pie -static -x c "$work/positive.i" -o "$work/gnu.elf"
"$cross_gcc" -fno-pie -no-pie -static "$work/mini.o" -o "$work/mini.elf"
for owner in gnu mini; do
    rc=0
    timeout 45s "$qemu" "$work/$owner.elf" >"$work/$owner.out" \
        2>"$work/$owner.err" || rc=$?
    if test "$rc" -ne 42; then
        echo "RV64_GLOBAL_PLAN_GNU_ELF=FAIL runtime=$owner exit=$rc (wanted 42)" >&2
        exit 1
    fi
done
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"
echo 'RV64_GLOBAL_PLAN_GNU_ELF=PASS same_i=1 gnu_as=1 ld_r=1 readelf_sizes=13 reloc=1 gnu_qemu_diff=1 known_bss_divergence=EXCLUDED'
