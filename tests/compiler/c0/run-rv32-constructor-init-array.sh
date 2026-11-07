#!/usr/bin/env bash
# Two GNU constructors (internal and external) need ELF32 .init_array entries.
# Toolchain absence is NOT_TESTED; compile/link/readelf/QEMU divergence is FAIL.
set -Eeuo pipefail

for tool in riscv64-linux-gnu-gcc riscv64-linux-gnu-ld \
            riscv64-linux-gnu-readelf qemu-riscv32 timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "RV32_CONSTRUCTOR=NOT_TESTED missing_tool=$tool"
        exit 0
    fi
done

: "${MINIC_CC:?MINIC_CC must refer to the MiniC compiler}"
if [[ ! -x "$MINIC_CC" ]]; then
    echo 'RV32_CONSTRUCTOR=FAIL minic_compiler_missing' >&2
    exit 1
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -std=gnu11 \
    -ffreestanding -fno-builtin -E -P -x c \
    "$root/tests/compiler/c0/rv32_constructor_init_array.c" -o "$work/input.i"

"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/input.i" -o "$work/minic.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
    -c "$work/minic.s" -o "$work/minic.o"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O1 -std=gnu11 \
    -ffreestanding -fno-builtin -fno-stack-protector -x c \
    -c "$work/input.i" -o "$work/gnu.o"

cat > "$work/start.s" <<'ASM'
.text
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  la s0, __init_array_start
  la s1, __init_array_end
1:
  beq s0, s1, 2f
  lw t0, 0(s0)
  addi s0, s0, 4
  jalr ra, 0(t0)
  j 1b
2:
  call main
  li a7, 93
  ecall
ASM
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
    -c "$work/start.s" -o "$work/start.o"

for mode in gnu minic; do
    # Static ELF32 link with real GNU AS/LD; no host or prebuilt MiniAS/LD.
    riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
        -o "$work/$mode.elf" "$work/start.o" "$work/$mode.o"

    if ! riscv64-linux-gnu-readelf -SW "$work/$mode.elf" |
         grep -Eq '[[:space:]]INIT_ARRAY[[:space:]]'; then
        echo "RV32_CONSTRUCTOR=FAIL mode=$mode missing_elf_init_array" >&2
        exit 1
    fi
    if [[ "$mode" == minic ]] &&
       ! riscv64-linux-gnu-readelf -rW "$work/minic.o" |
           grep -q 'R_RISCV_32'; then
        echo 'RV32_CONSTRUCTOR=FAIL missing_init_array_relocation' >&2
        exit 1
    fi

    set +e
    timeout 30s qemu-riscv32 "$work/$mode.elf"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "RV32_CONSTRUCTOR=FAIL mode=$mode qemu_exit=$rc" >&2
        exit 1
    fi
    echo "RV32_CONSTRUCTOR_MODE=PASS mode=$mode ELF32=1 INIT_ARRAY=1 GNU_AS_LD=1 QEMU=1"
done
echo 'RV32_CONSTRUCTOR=PASS constructors=2 internal=1 external=1 nonconstructor=1 GNU_C_oracle=1 MiniC_C=1'
