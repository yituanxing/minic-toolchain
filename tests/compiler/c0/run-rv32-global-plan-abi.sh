#!/bin/sh
# RV32 GlobalEmissionPlan migration gate. One host-GNU .i is consumed by the
# new MiniC and the optional C39 baseline. The GNU ELF32/QEMU stage is opt-in
# and reports BLOCKED, never PASS, when the cross toolchain is unavailable.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
baseline=${MINIC_RV32_GLOBAL_BASELINE_CC:-}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/rv32-global-plan-abi
positive="$root/tests/compiler/c0/rv32_global_plan_abi.c"
bad_section="$root/tests/compiler/c0/invalid_rv32_global_plan_section_injection.c"
mkdir -p "$work"

test -x "$minic" || {
    echo "RV32_GLOBAL_PLAN_SOURCE=BLOCKED missing_minic=$minic" >&2
    exit 2
}
if test -n "$baseline"; then
    test -x "$baseline" || {
        echo "RV32_GLOBAL_PLAN_DIFF=BLOCKED missing_baseline=$baseline" >&2
        exit 2
    }
fi

"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$positive"
"$host_cc" -std=gnu11 -E -P -x c "$positive" -o "$work/positive.i"
"$host_cc" -std=gnu11 -E -P -x c "$bad_section" -o "$work/bad-section.i"

"$minic" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/positive.i" -o "$work/mini.s"
test -s "$work/mini.s"
for symbol in rv32_plan_anchor rv32_plan_wide rv32_plan_halves \
              rv32_plan_addend rv32_plan_zeros rv32_plan_empty_record \
              rv32_plan_empty_array rv32_plan_named_bss rv32_plan_named_data \
              rv32_plan_weak_zero rv32_plan_weak_value rv32_plan_weak_array \
              rv32_plan_nested rv32_plan_pointer_grid; do
    grep -Fq "$symbol:" "$work/mini.s"
    grep -Fq ".size $symbol," "$work/mini.s"
done
grep -Fq '.size rv32_plan_empty_record, 0' "$work/mini.s"
grep -Fq '.size rv32_plan_empty_array, 0' "$work/mini.s"
grep -Fq '  .8byte 18364758544493064720' "$work/mini.s"
grep -Fq '  .4byte rv32_plan_anchor+8' "$work/mini.s"
grep -Fq '  .4byte rv32_plan_anchor+12' "$work/mini.s"
grep -Fq '.section .bss.minic.rv32.plan,"aw",@progbits' "$work/mini.s"
for symbol in rv32_plan_weak_zero rv32_plan_weak_value rv32_plan_weak_array; do
    grep -Fq ".weak $symbol" "$work/mini.s"
done

rm -f "$work/mini-bad-section.s"
if "$minic" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/bad-section.i" -o "$work/mini-bad-section.s" \
    >"$work/mini-bad-section.out" 2>"$work/mini-bad-section.err"; then
    echo 'RV32_GLOBAL_PLAN_SOURCE=FAIL section injection admitted' >&2
    exit 1
fi
test ! -e "$work/mini-bad-section.s"

if test -n "$baseline"; then
    "$baseline" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
        -S "$work/positive.i" -o "$work/baseline.s"
    if ! cmp -s "$work/baseline.s" "$work/mini.s"; then
        echo 'RV32_GLOBAL_PLAN_DIFF=FAIL changed positive .s bytes' >&2
        diff -u "$work/baseline.s" "$work/mini.s" || true
        exit 1
    fi
    rm -f "$work/baseline-bad-section.s"
    if "$baseline" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
        -S "$work/bad-section.i" -o "$work/baseline-bad-section.s" \
        >"$work/baseline-bad-section.out" \
        2>"$work/baseline-bad-section.err"; then
        echo 'RV32_GLOBAL_PLAN_DIFF=FAIL baseline admitted section injection' >&2
        exit 1
    fi
    test ! -e "$work/baseline-bad-section.s"
    echo 'RV32_GLOBAL_PLAN_DIFF=PASS source_same_i=1 asm_byte_identical=1 section_injection_rejected=1'
fi

echo 'RV32_GLOBAL_PLAN_SOURCE=PASS objects=14 weak=3 zero_size=2 named_bss_progbits=1 nested=1 reloc_addend32=1 scalar_8byte=1 injection_rejected=1 GNU_ELF=NOT_RUN'
if test "${MINIC_RV32_GLOBAL_GNU_ELF:-0}" != 1; then
    exit 0
fi

gnu_cc=${RV32_GNU_GCC:-riscv64-linux-gnu-gcc}
gnu_as=${RV32_GNU_AS:-riscv64-linux-gnu-as}
gnu_ld=${RV32_GNU_LD:-riscv64-linux-gnu-ld}
gnu_readelf=${RV32_GNU_READELF:-riscv64-linux-gnu-readelf}
qemu=${RV32_QEMU:-qemu-riscv32}
for required in "$gnu_cc" "$gnu_as" "$gnu_ld" "$gnu_readelf" "$qemu" timeout; do
    if ! command -v "$required" >/dev/null 2>&1; then
        echo "RV32_GLOBAL_PLAN_GNU_ELF=BLOCKED missing=$required (not PASS)" >&2
        exit 2
    fi
done

"$gnu_cc" -march=rv32i -mabi=ilp32 -O0 -ffreestanding -fno-builtin \
    -fno-pic -fno-pie -c -x c "$work/positive.i" -o "$work/gnu.o"
"$gnu_as" -march=rv32i -mabi=ilp32 "$work/mini.s" -o "$work/mini.o"
"$gnu_ld" -m elf32lriscv -r "$work/mini.o" -o "$work/mini-reloc-linked.o"
for owner in gnu mini; do
    "$gnu_readelf" -h "$work/$owner.o" >"$work/$owner.elf-header"
    "$gnu_readelf" -sW "$work/$owner.o" >"$work/$owner.symbols"
    "$gnu_readelf" -rW "$work/$owner.o" >"$work/$owner.relocs"
    "$gnu_readelf" -SW "$work/$owner.o" >"$work/$owner.sections"
    grep -Eq 'Class:[[:space:]]+ELF32' "$work/$owner.elf-header"
done
"$gnu_readelf" -rW "$work/mini-reloc-linked.o" >"$work/mini-linked.relocs"
grep -Fq 'R_RISCV_32' "$work/mini.relocs"
grep -Fq 'R_RISCV_32' "$work/mini-linked.relocs"
if grep -Eq 'R_RISCV_64' "$work/mini.relocs"; then
    echo 'RV32_GLOBAL_PLAN_GNU_ELF=FAIL 64-bit relocation leaked into ELF32' >&2
    exit 1
fi
for symbol in rv32_plan_anchor rv32_plan_wide rv32_plan_halves \
              rv32_plan_addend rv32_plan_zeros rv32_plan_empty_record \
              rv32_plan_empty_array rv32_plan_named_bss rv32_plan_named_data \
              rv32_plan_weak_zero rv32_plan_weak_value rv32_plan_weak_array \
              rv32_plan_nested rv32_plan_pointer_grid; do
    gnu_size=$(awk -v name="$symbol" '$NF == name { print $3; exit }' "$work/gnu.symbols")
    mini_size=$(awk -v name="$symbol" '$NF == name { print $3; exit }' "$work/mini.symbols")
    if test -z "$gnu_size" || test "$gnu_size" != "$mini_size"; then
        echo "RV32_GLOBAL_PLAN_GNU_ELF=FAIL size $symbol GNU=$gnu_size MiniC=$mini_size" >&2
        exit 1
    fi
done
for symbol in rv32_plan_weak_zero rv32_plan_weak_value rv32_plan_weak_array; do
    grep -Eq "WEAK[[:space:]]+DEFAULT[[:space:]]+[[:digit:]]+[[:space:]]+$symbol$" \
        "$work/mini.symbols"
done

# Known C39 difference: explicit .bss.* is still PROGBITS in RV32, whereas
# GNU chooses NOBITS. Report both types; do not call this GNU section parity.
gnu_bss=$(awk '$3 == ".bss.minic.rv32.plan" { print $4; exit }' "$work/gnu.sections")
mini_bss=$(awk '$3 == ".bss.minic.rv32.plan" { print $4; exit }' "$work/mini.sections")
echo "RV32_GLOBAL_PLAN_BSS_PARITY=OBSERVED GNU=$gnu_bss MiniC=$mini_bss (not PASS)"

"$gnu_cc" -march=rv32i -mabi=ilp32 -c -x assembler \
    "$root/tests/compiler/c0/rv32_global_plan_start.S" -o "$work/start.o"
"$gnu_ld" -m elf32lriscv -static -e _start \
    -o "$work/gnu.elf" "$work/start.o" "$work/gnu.o"
"$gnu_ld" -m elf32lriscv -static -e _start \
    -o "$work/mini.elf" "$work/start.o" "$work/mini.o"
for owner in gnu mini; do
    rc=0
    timeout 45s "$qemu" "$work/$owner.elf" >"$work/$owner.out" \
        2>"$work/$owner.err" || rc=$?
    if test "$rc" -ne 42; then
        echo "RV32_GLOBAL_PLAN_GNU_ELF=FAIL runtime=$owner exit=$rc (wanted 42)" >&2
        exit 1
    fi
done
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"
echo 'RV32_GLOBAL_PLAN_GNU_ELF=PASS same_i=1 GNU_AS_LD=1 ELF32=1 QEMU_diff=1 sizes=14 weak=3 reloc32=1 scalar_8byte=1 known_bss_divergence=EXCLUDED'
