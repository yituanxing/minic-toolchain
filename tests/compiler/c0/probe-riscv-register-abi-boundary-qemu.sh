#!/usr/bin/env bash
# One-object GNU/MiniC differential for RISC-V physical scalar ABI ingress
# and egress. No libc, Linux Image, or full matrix rebuild is needed.
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC to a current MiniC compiler executable}"
cross="${RISCV_CC:-riscv64-linux-gnu-gcc}"
linker="${RISCV_LD:-riscv64-linux-gnu-ld}"
for tool in "$cross" "$linker" qemu-riscv32 qemu-riscv64 timeout; do
    command -v "$tool" >/dev/null || {
        echo "RISCV_SCALAR_ABI=FAIL mandatory_tool_missing=$tool" >&2
        exit 1
    }
done
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root/tests/compiler/c0/riscv_abi_register_boundary.c"
driver_file="$root/tests/compiler/c0/riscv_abi_register_boundary_driver.c"
raw_file="$root/tests/compiler/c0/riscv_abi_register_boundary.S"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for arch in 32 64; do
    if [[ "$arch" == 32 ]]; then
        target=riscv32-linux
        abi=(-march=rv32i -mabi=ilp32)
        emulation=elf32lriscv
        qemu=qemu-riscv32
        rv64=0
    else
        target=riscv64-linux
        abi=(-march=rv64i -mabi=lp64)
        emulation=elf64lriscv
        qemu=qemu-riscv64
        rv64=1
    fi
    base="$work/rv$arch"
    "$cross" "${abi[@]}" -std=gnu11 -DABI_RV64="$rv64" \
        -E -P -x c "$source_file" -o "$base.i"
    "$MINIC_CC" --target="$target" "${abi[@]}" -S \
        "$base.i" -o "$base.minic.s"
    "$cross" "${abi[@]}" -c "$base.minic.s" -o "$base.minic.o"
    "$cross" "${abi[@]}" -DABI_RV64="$rv64" -std=gnu11 \
        -O1 -fno-inline -ffreestanding -fno-builtin \
        -fno-stack-protector -fno-pic -c "$source_file" -o "$base.gnu.o"
    "$cross" "${abi[@]}" -DABI_RV64="$rv64" -std=gnu11 \
        -O1 -fno-inline -ffreestanding -fno-builtin \
        -fno-stack-protector -fno-pic -c "$driver_file" -o "$base.driver.o"
    "$cross" "${abi[@]}" -DABI_RV64="$rv64" -c \
        "$raw_file" -o "$base.raw.o"

    for producer in gnu minic; do
        "$linker" -m "$emulation" -static -e _start \
            -o "$base.$producer.elf" \
            "$base.raw.o" "$base.driver.o" "$base.$producer.o"
        status=0
        timeout 30s "$qemu" "$base.$producer.elf" || status=$?
        if (( status != 0 )); then
            echo "RISCV_SCALAR_ABI=FAIL arch=RV$arch producer=$producer case=$status" >&2
            exit 1
        fi
        echo "RISCV_SCALAR_ABI_CASE=PASS arch=RV$arch producer=$producer"
    done
done
echo 'RISCV_SCALAR_ABI=PASS RV32/RV64 GNU_C vs MiniC+GNU_AS 32bit-signext 8/16bit-ASM packed/register ABI calls+return+stack true-QEMU no-libc'
