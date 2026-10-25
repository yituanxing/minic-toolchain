#!/usr/bin/env bash
# Genuine RV32I ILP32 GCC and MiniC, GNU-AS/MiniAS, GNU-LD and QEMU.
set -Eeuo pipefail
: "$MINIC_CC"
: "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/dynamic.c" <<'SRC'
int sum12(int a,int b,int c,int d,int e,int f,int g,int h,
          int i,int j,int k,int l) {
    return a+b+c+d+e+f+g+h+i+j+k+l;
}
int dynamic_frame(unsigned n) {
    volatile unsigned keep = 13U;
    unsigned char *first = (unsigned char *)__builtin_alloca(n);
    first[0] = 7;
    first[n - 1U] = 11;
    unsigned char *second = (unsigned char *)__builtin_alloca(n + 19U);
    second[0] = 13;
    second[n + 18U] = 17;
    if (keep != 13U) return 0;
    return first[0] + first[n - 1U] + second[0] + second[n + 18U] +
           sum12(1,2,3,4,5,6,7,8,9,10,11,12);
}
SRC
cat >"$work/start.s" <<'ASM'
.text
.globl _start
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    li s0, 0x123456
    li a0, 32
    call dynamic_frame
    li t0, 126
    bne a0, t0, fail
    li t0, 0x123456
    bne s0, t0, fail
    li a0, 0
    j done
fail:
    li a0, 1
done:
    li a7, 93
    ecall
ASM
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -O0 -ffreestanding -E -P "$work/dynamic.c" -o "$work/dynamic.i"
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -O0 -ffreestanding -c "$work/dynamic.c" -o "$work/reference.o"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 -S "$work/dynamic.i" -o "$work/dynamic.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c "$work/dynamic.s" -o "$work/gnu-as.o"
"$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/minias.o" "$work/dynamic.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c "$work/start.s" -o "$work/start.o"
for mode in reference gnu-as minias; do
    riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
      -o "$work/$mode.elf" "$work/start.o" "$work/$mode.o"
    if ! timeout 30s qemu-riscv32 "$work/$mode.elf"; then
        echo "RV32_DYNAMIC_FRAME=FAIL mode=$mode" >&2
        exit 1
    fi
    echo "RV32_DYNAMIC_FRAME_MODE=PASS mode=$mode"
done
echo 'RV32_DYNAMIC_FRAME=PASS GCC=1 MiniC=1 GNU_AS=1 MiniAS=1 nested_alloca=1 stack_call=1 saved_s0=1 QEMU=1'
