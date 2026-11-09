#!/usr/bin/env bash
# Full original GNU assembler/linker and real QEMU for MiniC-selected RISC-V.
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC to the compiled minic-cc}"
cross=${RISCV_CC:-riscv64-linux-gnu-gcc}
linker=${RISCV_LD:-riscv64-linux-gnu-ld}
for tool in "$cross" "$linker" qemu-riscv32 qemu-riscv64 timeout; do
    command -v "$tool" >/dev/null || {
        echo "RV_ADJACENT_STACK_QEMU=FAIL missing_tool=$tool" >&2
        exit 1
    }
done
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root/tests/compiler/c0/riscv_adjacent_private_stack_forward.c"
driver="$root/tests/compiler/c0/riscv_adjacent_private_stack_forward_driver.c"
start="$root/tests/compiler/c0/riscv_adjacent_private_stack_forward_start.S"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for arch in 32 64; do
    if [[ "$arch" == 32 ]]; then
        abi=(-march=rv32i -mabi=ilp32)
        target=riscv32-linux
        emulation=elf32lriscv
        qemu=qemu-riscv32
    else
        abi=(-march=rv64i -mabi=lp64)
        target=riscv64-linux
        emulation=elf64lriscv
        qemu=qemu-riscv64
    fi
    base="$work/rv$arch"
    "$cross" "${abi[@]}" -std=gnu11 -E -P -x c "$source_file" -o "$base.i"
    "$MINIC_CC" --target="$target" "${abi[@]}" -S "$base.i" -o "$base.s"
    "$cross" "${abi[@]}" -c "$base.s" -o "$base.minic.o"
    "$cross" "${abi[@]}" -O1 -ffreestanding -fno-builtin -fno-pic \
        -fno-stack-protector -c "$driver" -o "$base.driver.o"
    "$cross" "${abi[@]}" -c "$start" -o "$base.start.o"
    "$linker" -m "$emulation" -static -e _start \
        -o "$base.elf" "$base.start.o" "$base.driver.o" "$base.minic.o"
    status=0
    timeout 30s "$qemu" "$base.elf" || status=$?
    if (( status != 0 )); then
        echo "RV_ADJACENT_STACK_QEMU=FAIL arch=RV$arch exit=$status" >&2
        exit 1
    fi
    echo "RV_ADJACENT_STACK_QEMU_CASE=PASS arch=RV$arch producer=MiniC GNU_AS_LD=true QEMU=true"
done
echo 'RV_ADJACENT_STACK_QEMU=PASS RV32I_ILP32 RV64I_LP64 adjacent+call+asm+branch+large_frame original_GNU_QEMU'
