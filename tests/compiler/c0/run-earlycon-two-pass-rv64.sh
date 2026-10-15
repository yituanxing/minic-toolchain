#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIC:?}"
: "${BUILD_DIR:?}"
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
ld=${RISCV_LD:-riscv64-linux-gnu-ld}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work="$BUILD_DIR/earlycon-two-pass"
mkdir -p "$work"
src="$root/tests/compiler/c0/earlycon_two_pass_runtime.c"
cat >"$work/start.s" <<'RISCV_START'
.section .text.start,"ax",@progbits
.globl _start
_start:
    # Freestanding RISC-V _start must initialize gp: GCC -O0 legitimately
    # addresses .sdata/.sbss globals through gp-relative relaxation.
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    call earlycon_probe_entry
    li a7, 93
    ecall
RISCV_START
"$cc" -c -x assembler "$work/start.s" -o "$work/start.o"
"$cc" -E -P -x c "$src" -o "$work/input.i"
"$cc" -O0 -c "$work/input.i" -o "$work/gcc.o"
"$ld" -static -e _start -o "$work/gcc" "$work/start.o" "$work/gcc.o"
if ! timeout 8s "$qemu" "$work/gcc"; then
    echo "EARLYCON_TWO_PASS_GCC_CONTROL=FAIL" >&2
    exit 1
fi
echo "EARLYCON_TWO_PASS_GCC_CONTROL=PASS"
"$MINIC" -S "$work/input.i" -o "$work/minic.s"
"$cc" -c -x assembler "$work/minic.s" -o "$work/minic.o"
"$ld" -static -e _start -o "$work/minic" "$work/start.o" "$work/minic.o"
if timeout 8s "$qemu" "$work/minic"; then
    echo "EARLYCON_TWO_PASS_MINIC=PASS cases=2"
else
    rc=$?
    echo "EARLYCON_TWO_PASS_MINIC=FAIL rc=$rc gcc_control=PASS" >&2
    exit 1
fi

# Second case preserves the actual Linux earlycon descriptor loop, including
# pointer induction, continues, nested conditionals and a two-pass goto.
"$cc" -E -P -x c "$root/tests/compiler/c0/earlycon_table_goto_runtime.c" -o "$work/table.i"
"$cc" -O0 -fno-stack-protector -c "$work/table.i" -o "$work/table-gcc.o"
sed 's/earlycon_probe_entry/earlycon_table_probe_entry/' "$work/start.s" >"$work/table-start.s"
"$cc" -c -x assembler "$work/table-start.s" -o "$work/table-start.o"
"$ld" -static -e _start -o "$work/table-gcc" "$work/table-start.o" "$work/table-gcc.o"
if ! timeout 8s "$qemu" "$work/table-gcc"; then
    echo "EARLYCON_TABLE_GCC_CONTROL=FAIL" >&2
    exit 1
fi
echo "EARLYCON_TABLE_GCC_CONTROL=PASS"
"$MINIC" -S "$work/table.i" -o "$work/table-minic.s"
"$cc" -c -x assembler "$work/table-minic.s" -o "$work/table-minic.o"
"$ld" -static -e _start -o "$work/table-minic" "$work/table-start.o" "$work/table-minic.o"
if timeout 8s "$qemu" "$work/table-minic"; then
    echo "EARLYCON_TABLE_MINIC=PASS cases=2"
else
    rc=$?
    echo "EARLYCON_TABLE_MINIC=FAIL rc=$rc gcc_control=PASS" >&2
    exit 1
fi
