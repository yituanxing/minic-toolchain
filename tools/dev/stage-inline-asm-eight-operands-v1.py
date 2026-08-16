#!/usr/bin/env python3
from pathlib import Path

p = Path('src/target/riscv64/codegen_inline_asm.c')
s = p.read_text()
old = '#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 6U'
assert s.count(old) == 1
p.write_text(s.replace(old, '#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 8U', 1))

Path('tests/compiler/c0/gnu_inline_asm_eight_operands.c').write_text(r'''unsigned long sbi_eight_operand_shape(unsigned long v0,
                                           unsigned long v1,
                                           unsigned long v2,
                                           unsigned long v3,
                                           unsigned long v4,
                                           unsigned long v5,
                                           unsigned long v6,
                                           unsigned long v7) {
    register unsigned long a0 asm("a0") = v0;
    register unsigned long a1 asm("a1") = v1;
    register unsigned long a2 asm("a2") = v2;
    register unsigned long a3 asm("a3") = v3;
    register unsigned long a4 asm("a4") = v4;
    register unsigned long a5 asm("a5") = v5;
    register unsigned long a6 asm("a6") = v6;
    register unsigned long a7 asm("a7") = v7;

    asm volatile("add %0, %0, %2\n\t# sbi %1 %3 %4 %5 %6 %7"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    return a0 + a1;
}
''')

Path('tests/compiler/c0/run-gnu-inline-asm-eight-operands.sh').write_text(r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
rv_cc=${RV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-eight-operands
rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_inline_asm_eight_operands.c" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/probe.s"
test -s "$work/probe.s"
grep -F 'add a0, a0, a2' "$work/probe.s" >/dev/null
grep -F '# sbi a1 a3 a4 a5 a6 a7' "$work/probe.s" >/dev/null
"$rv_cc" -c "$work/probe.s" -o "$work/probe.o"
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_eight_operands total=8 outputs=2 inputs=6 bindings=a0-a7 assembly=1'
''')

print('staged RV64 inline-asm eight-operand capacity with SBI-shaped regression')
