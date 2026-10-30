#!/bin/sh
# GNU PIC strict differential: undefined hidden external object/function
# references must be nonpreemptible and retain STV_HIDDEN in ELF. Defaults
# require AArch64 GNU cross tools; MINIC_SOURCE_ONLY=1 is an explicit,
# visibly weaker local preflight, NEVER a Linux vDSO ELF/link acceptance.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-gcc}
cross_cc=${AARCH64_GNU_CC:-aarch64-linux-gnu-gcc}
cross_readelf=${AARCH64_GNU_READELF:-aarch64-linux-gnu-readelf}
qemu=${QEMU_AARCH64:-qemu-aarch64}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/elf_hidden_undefined_pic.c"
owner_file="$root/tests/compiler/c0/elf_hidden_undefined_pic_owner.c"

"$host_cc" -E -P -std=gnu11 "$source_file" -o "$work/probe.i"
"$minic_cc" --target=aarch64-linux -fPIC -S "$work/probe.i" -o "$work/a64-mini.s"
"$minic_cc" --target=aarch64-linux -fPIE -S "$work/probe.i" -o "$work/a64-pie.s"
"$minic_cc" --target=aarch64-linux -fno-pie -S "$work/probe.i" \
    -o "$work/a64-nonpic.s"
cmp "$work/a64-mini.s" "$work/a64-pie.s"

for symbol in hidden_data hidden_function; do
    grep -Fxq ".hidden $symbol" "$work/a64-mini.s"
    grep -Fq "  adrp x9, $symbol" "$work/a64-mini.s"
    if grep -Fq ":got:$symbol" "$work/a64-mini.s"; then
        echo "FAIL: hidden external $symbol materialized via preemptible GOT" >&2
        exit 1
    fi
done
for symbol in default_data default_function; do
    grep -Fq "  adrp x9, :got:$symbol" "$work/a64-mini.s"
    grep -Fq "  adrp x9, $symbol" "$work/a64-nonpic.s"
    if grep -Fxq ".hidden $symbol" "$work/a64-mini.s"; then
        echo "FAIL: default-visible $symbol improperly marked hidden" >&2
        exit 1
    fi
done

# ELF visibility belongs to the declaration, not a particular ISA emitter.
# RV32 currently rejects -fPIC by target policy, so test its declaration
# serialization in the supported non-PIC mode without masking that limit.
for target in riscv32-linux riscv64-linux loongarch64-linux; do
    case "$target" in
        riscv32-linux) opts='-march=rv32i -mabi=ilp32' ;;
        riscv64-linux) opts='-march=rv64i -mabi=lp64' ;;
        *) opts='' ;;
    esac
    "$minic_cc" --target="$target" $opts -S "$work/probe.i" \
        -o "$work/$target.s"
    for symbol in hidden_data hidden_function; do
        grep -Fxq ".hidden $symbol" "$work/$target.s"
    done
    for symbol in default_data default_function; do
        if grep -Fxq ".hidden $symbol" "$work/$target.s"; then
            echo "FAIL: $target default $symbol unexpectedly hidden" >&2
            exit 1
        fi
    done
done

# A native x86 baseline validates that the same C API works with the default
# and hidden definitions in a distinct object, including function pointers.
"$minic_cc" --target=x86_64-linux -fPIC -S "$work/probe.i" -o "$work/x86-mini.s"
"$host_cc" -O0 -fno-pie -no-pie "$source_file" "$owner_file" -o "$work/x86-gnu"
"$host_cc" -fno-pie -no-pie -x assembler "$work/x86-mini.s" \
    -x c "$owner_file" -o "$work/x86-mini"
"$work/x86-gnu"
"$work/x86-mini"

if [ "${MINIC_SOURCE_ONLY:-0}" = 1 ]; then
    echo 'AARCH64_HIDDEN_EXTERN=SOURCE_ONLY five_ISA_S=PASS x86_native=PASS ELF_and_QEMU=NOT_RUN'
    exit 0
fi
if ! command -v "$cross_cc" >/dev/null 2>&1 ||
   ! command -v "$cross_readelf" >/dev/null 2>&1; then
    echo "FAIL: strict AArch64 ELF validation requires $cross_cc and $cross_readelf" >&2
    exit 2
fi

"$cross_cc" -fPIC -O0 -c -x c "$work/probe.i" -o "$work/a64-gnu.o"
"$cross_cc" -fPIC -c -x assembler "$work/a64-mini.s" -o "$work/a64-mini.o"
"$cross_cc" -fPIC -O0 -c "$owner_file" -o "$work/a64-owner.o"
"$cross_readelf" -Ws "$work/a64-gnu.o" >"$work/gnu.sym"
"$cross_readelf" -Ws "$work/a64-mini.o" >"$work/mini.sym"
"$cross_readelf" -Wr "$work/a64-gnu.o" >"$work/gnu.reloc"
"$cross_readelf" -Wr "$work/a64-mini.o" >"$work/mini.reloc"

for symbol in hidden_data hidden_function; do
    grep -E "GLOBAL[[:space:]]+HIDDEN[[:space:]]+UND[[:space:]]+$symbol" \
        "$work/gnu.sym" >/dev/null
    grep -E "GLOBAL[[:space:]]+HIDDEN[[:space:]]+UND[[:space:]]+$symbol" \
        "$work/mini.sym" >/dev/null
    grep -E "R_AARCH64_(ADR_PREL_PG_HI21|ADR_PREL_LO21)[[:space:]]+[^[:space:]]+[[:space:]]+$symbol" \
        "$work/mini.reloc" >/dev/null
    if grep -E "R_AARCH64_(ADR_GOT_PAGE|LD64_GOT_LO12_NC)[[:space:]]+[^[:space:]]+[[:space:]]+$symbol" \
        "$work/mini.reloc" >/dev/null; then
        echo "FAIL: hidden $symbol still has AArch64 GOT relocations" >&2
        exit 1
    fi
done
for symbol in default_data default_function; do
    grep -E "GLOBAL[[:space:]]+DEFAULT[[:space:]]+UND[[:space:]]+$symbol" \
        "$work/mini.sym" >/dev/null
    grep -E "R_AARCH64_ADR_GOT_PAGE[[:space:]]+[^[:space:]]+[[:space:]]+$symbol" \
        "$work/mini.reloc" >/dev/null
done

"$cross_cc" -shared -fPIC "$work/a64-gnu.o" "$work/a64-owner.o" -o "$work/a64-gnu.so"
"$cross_cc" -shared -fPIC "$work/a64-mini.o" "$work/a64-owner.o" -o "$work/a64-mini.so"
if command -v "$qemu" >/dev/null 2>&1; then
    "$cross_cc" -static "$work/a64-gnu.o" "$work/a64-owner.o" -o "$work/a64-gnu"
    "$cross_cc" -static "$work/a64-mini.o" "$work/a64-owner.o" -o "$work/a64-mini"
    timeout 40s "$qemu" -L /usr/aarch64-linux-gnu "$work/a64-gnu"
    timeout 40s "$qemu" -L /usr/aarch64-linux-gnu "$work/a64-mini"
    echo 'AARCH64_HIDDEN_EXTERN=PASS PIC=1 five_ISA_S=PASS GNU_AS_LD=PASS ELF_visibility_reloc=PASS QEMU=PASS x86_native=PASS'
else
    echo 'AARCH64_HIDDEN_EXTERN=PASS PIC=1 five_ISA_S=PASS GNU_AS_LD=PASS ELF_visibility_reloc=PASS QEMU=NOT_RUN x86_native=PASS'
fi
