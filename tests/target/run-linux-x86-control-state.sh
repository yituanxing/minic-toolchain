#!/bin/sh
# The Linux 6.6 x86 alternative/control-state contract batch is compile-only.
# Never execute its privileged WRMSR, XSETBV, CLI, STI or HLT instructions.
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

for fixture in \
    x86_64_linux_alternative_wrmsr \
    x86_64_linux_cpu_feature_goto \
    x86_64_linux_xstate_control \
    x86_64_linux_fxsave \
    x86_64_linux_fpu_boot
do
    "$host_cc" -E -P "$source_root/tests/target/$fixture.c" \
        -o "$workdir/$fixture.i"
    "$minic_cc" -S "$workdir/$fixture.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/$fixture.s"
    "$host_cc" -c -x assembler "$workdir/$fixture.s" \
        -o "$workdir/$fixture.o"
done

objdump -d "$workdir/x86_64_linux_xstate_control.o" > "$workdir/xstate.dump"
objdump -d "$workdir/x86_64_linux_fxsave.o" > "$workdir/fxsave.dump"
objdump -d "$workdir/x86_64_linux_fpu_boot.o" > "$workdir/fpu.dump"
readelf -SW "$workdir/x86_64_linux_cpu_feature_goto.o" > "$workdir/cpu.sections"
readelf -x .altinstructions "$workdir/x86_64_linux_cpu_feature_goto.o" > "$workdir/cpu.alt.hex"
readelf -SW "$workdir/x86_64_linux_alternative_wrmsr.o" > "$workdir/msr.sections"

for instruction in xgetbv xsetbv
do
    if ! grep -q "$instruction" "$workdir/xstate.dump"; then
        printf 'FAIL: x86 Linux extended state instruction %s absent\n' "$instruction" >&2
        exit 1
    fi
done
for instruction in 'fxsave ' 'fxsave64 '
do
    if ! grep -q "$instruction" "$workdir/fxsave.dump"; then
        printf 'FAIL: x86 Linux FXSAVE encoding %s absent\n' "$instruction" >&2
        exit 1
    fi
done
for instruction in fninit fnstsw fnstcw cli sti hlt
do
    if ! grep -q "$instruction" "$workdir/fpu.dump"; then
        printf 'FAIL: x86 Linux FPU/interrupt instruction %s absent\n' "$instruction" >&2
        exit 1
    fi
done
for section in .altinstructions .altinstr_replacement .altinstr_aux
do
    if ! grep -Fq "$section" "$workdir/cpu.sections"; then
        printf 'FAIL: Linux CPU feature section %s absent\n' "$section" >&2
        exit 1
    fi
done
for section in .altinstructions .altinstr_replacement
do
    if ! grep -Fq "$section" "$workdir/msr.sections"; then
        printf 'FAIL: Linux WRMSR alternative section %s absent\n' "$section" >&2
        exit 1
    fi
done
if ! grep -Fq '0000bb00' "$workdir/cpu.alt.hex" ||
   ! grep -Fq '00008c01' "$workdir/cpu.alt.hex"; then
    printf 'FAIL: Linux CPU feature patch table lost features 187 or 396\n' >&2
    exit 1
fi

"$host_cc" -E -P "$source_root/tests/target/core_switch_detached_goto_reentry.c" \
    -o "$workdir/core_switch_detached.i"
for arch in x86_64 aarch64 loongarch64
do
    "$minic_cc" -S "$workdir/core_switch_detached.i" --target="$arch-linux" \
        -o "$workdir/core_switch_detached.$arch.s"
done

printf 'PASS: Linux x86 WRMSR/CPU alternatives, xstate, FXSAVE, x87, IRQ and 3-arch switch goto reentry\n'
