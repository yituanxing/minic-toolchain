#!/usr/bin/env bash
# A resolved TargetSpec is a per-invocation immutable value. Source-local ISA
# state and emission-time ELF flags have separate owners and lifetimes.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to the exact-head minic-as binary}"

compiler=${CC:-cc}
flags=(-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
       -Iassembler/src -x c -fsyntax-only -)
reject() {
    local name=$1 source=$2 output
    if output=$(printf '%s\n' "$source" | "$compiler" "${flags[@]}" 2>&1); then
        echo "mutable TargetSpec consumer compiled: $name" >&2
        exit 1
    fi
    test -n "$output"
}

reject object_class '
#include "minias_internal.h"
void mutate(MiniAs *as) { as->target_spec->object.elf32 = true; }
'
reject initial_flags '
#include "minias_internal.h"
void mutate(MiniAs *as) { as->target_spec->object.initial_elf_flags |= 1U; }
'
reject object_machine '
#include "minias_internal.h"
void mutate(MiniAs *as) { as->target_spec->object.machine = 3U; }
'
reject relocation_format '
#include "minias_internal.h"
void mutate(MiniAs *as) { as->target_spec->object.rel_relocations = true; }
'
reject private_profile '
#include "minias_internal.h"
void mutate(MiniAs *as) { as->target_spec->profile[0] = 1U; }
'
reject old_class_shadow '
#include "minias_internal.h"
void mutate(MiniAs *as) { as->output_elf32 = true; }
'
reject old_machine_shadow '
#include "minias_internal.h"
void mutate(MiniAs *as) { as->object_machine = 3U; }
'

printf '%s\n' '
#include "minias_internal.h"
MiniAsTargetABIView observe(const MiniAs *as) {
    const MiniAsTargetSpec *spec = as->target_spec;
    MiniAsTargetABIView abi = minias_target_abi_view(as);
    if (spec->object.elf32 != abi.elf32) abi.elf32 = false;
    return abi;
}
' | "$compiler" "${flags[@]}"

# The same immutable object profile is observed by Target initialization,
# DWARF/CFI layout and the Writer. The target-local `.code*`/`.option`
# dialect cannot silently change ELF class, machine or REL/RELA kind.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf '.text\nnop\n' >"$work/nop.s"

check() {
    local name=$1 target=$2 march=$3 mabi=$4 class=$5 machine=$6
    local object="$work/$name.o" header="$work/$name.header"
    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$object" "$work/nop.s"
    readelf -h "$object" >"$header"
    grep -Eq "Class:[[:space:]]+ELF$class" "$header"
    grep -Eq "Machine:.*$machine" "$header"
}

check rv32 riscv rv32im ilp32 32 RISC-V
check rv64 riscv rv64imac lp64 64 RISC-V
check a64 aarch64 armv8-a lp64 64 AArch64
check amd64 x86_64 x86-64 sysv 64 'Advanced Micro Devices X86-64'
check i386 x86_64 i386 i386 32 'Intel 80386'
check loong loongarch64 loongarch64 lp64f 64 LoongArch

# ELF32 i386 uses REL; ELF64 AMD64 uses RELA, independently of `.code*`.
printf '.text\n.long external_symbol\n' >"$work/rel.s"
for name in amd64 i386; do
    if [[ "$name" == i386 ]]; then march=i386; mabi=i386
    else march=x86-64; mabi=sysv; fi
    "$MINIAS" --target=x86_64 "-march=$march" "-mabi=$mabi" \
        -o "$work/$name-rel.o" "$work/rel.s"
done
readelf -SW "$work/i386-rel.o" | grep -q '\.rel\.text'
readelf -SW "$work/amd64-rel.o" | grep -q '\.rela\.text'

# A source ISA mode change adds RVC output flags at emission, not by
# modifying the resolved TargetSpec or the selected ELF32/64 class.
printf '.text\n.option rvc\nc.li a0,1\n' >"$work/rvc.s"
"$MINIAS" --target=riscv -march=rv64imac -mabi=lp64 \
    -o "$work/rvc.o" "$work/rvc.s"
readelf -h "$work/rvc.o" | grep -Eq 'Flags:.*0x1([^[:xdigit:]]|$)'

echo 'MINIAS_TARGET_SPEC=PASS negative_compiles=7 positive=1 object_profiles=6 REL_RELA=2 source_RVC=1'
