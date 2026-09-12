#!/usr/bin/env bash
set -Eeuo pipefail

build="${BUILD_DIR:-build/minild-gc-output-class-v0}"
minild="${MINILD:-$build/bin/minic-ld}"
as="${RISCV_AS:-riscv64-linux-gnu-as}"
gnuld="${RISCV_LD:-riscv64-linux-gnu-ld}"
qemu="${QEMU_RISCV64:-qemu-riscv64}"
work="$build/gc-output-class-v0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/caller.s" <<'S'
.section .text.caller,"ax",@progbits
.globl _start
.type _start,@function
_start:
    j target
.size _start,.-_start
S

cat >"$work/filler.s" <<'S'
.section .rodata.live,"a",@progbits
.globl live_rodata
.type live_rodata,@object
live_rodata:
    .space 2097152, 0
.size live_rodata,.-live_rodata
S

cat >"$work/target.s" <<'S'
.section .text.target,"ax",@progbits
.globl target
.type target,@function
target:
    la t0, live_rodata
    li a0, 47
    li a7, 93
    ecall
.size target,.-target
S

"$as" -march=rv64gc -mabi=lp64d -o "$work/caller.o" "$work/caller.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/filler.o" "$work/filler.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/target.o" "$work/target.s"

# GNU's default linker script groups .text.* inputs into the .text output
# section and places .rodata afterwards. The direct JAL is therefore in range
# even though the raw input order is caller, 2 MiB rodata, target.
"$gnuld" -melf64lriscv -static --gc-sections --sort-section=alignment -e _start \
  -o "$work/gnu.elf" "$work/caller.o" "$work/filler.o" "$work/target.o"
set +e
"$qemu" "$work/gnu.elf"
gnu_rc=$?
set -e
test "$gnu_rc" -eq 47

echo "MINILD_GC_OUTPUT_CLASS_GNU=PASS qemu_rc=47"

# Current MiniLD is expected to reproduce the BusyBox failure class until its
# non-script default layout groups executable input sections separately from
# read-only data instead of globally flattening every RX section by input order.
set +e
"$minild" -melf64lriscv -static --gc-sections -e _start \
  -o "$work/mini.elf" "$work/caller.o" "$work/filler.o" "$work/target.o" \
  >"$work/minild.stdout" 2>"$work/minild.stderr"
mini_rc=$?
set -e

if test "$mini_rc" -eq 0; then
  set +e
  "$qemu" "$work/mini.elf"
  qemu_rc=$?
  set -e
  if test "$qemu_rc" -eq 47; then
    echo "MINILD_GC_OUTPUT_CLASS_V0=UNEXPECTED_PASS qemu_rc=47"
    exit 0
  fi
fi

grep -q 'R_RISCV_JAL-overflow' "$work/minild.stderr"
echo "MINILD_GC_OUTPUT_CLASS_V0=REPRODUCED mini_rc=$mini_rc expected=R_RISCV_JAL-overflow"
exit 1
