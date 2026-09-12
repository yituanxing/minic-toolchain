#!/usr/bin/env bash
set -Eeuo pipefail

build="${BUILD_DIR:-build/minild-gc-alignment-v0}"
minild="${MINILD:-$build/bin/minic-ld}"
as="${RISCV_AS:-riscv64-linux-gnu-as}"
gnuld="${RISCV_LD:-riscv64-linux-gnu-ld}"
qemu="${QEMU_RISCV64:-qemu-riscv64}"
readelf="${RISCV_READELF:-riscv64-linux-gnu-readelf}"
work="$build/gc-alignment-v0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/caller.s" <<'S'
.section .text.caller,"ax",@progbits
.p2align 4
.globl _start
.type _start,@function
_start:
    j target
.size _start,.-_start
S

cat >"$work/filler.s" <<'S'
.section .text.filler,"ax",@progbits
.globl live_filler
.type live_filler,@function
live_filler:
    ret
    .space 2097152, 0
.size live_filler,.-live_filler
S

cat >"$work/target.s" <<'S'
.section .text.target,"ax",@progbits
.p2align 4
.globl target
.type target,@function
target:
    la t0, live_filler
    li a0, 53
    li a7, 93
    ecall
.size target,.-target
S

"$as" -march=rv64gc -mabi=lp64d -o "$work/caller.o" "$work/caller.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/filler.o" "$work/filler.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/target.o" "$work/target.s"

"$readelf" -SW "$work/caller.o" >"$work/caller.sections"
"$readelf" -SW "$work/filler.o" >"$work/filler.sections"
"$readelf" -SW "$work/target.o" >"$work/target.sections"

# GNU BusyBox trylink uses --sort-section,alignment.  Freeze the semantic that
# higher-alignment executable input sections sort ahead of lower-alignment code
# within the .text output class.  This keeps the direct JAL from caller to
# target in range even though a 2 MiB live executable section appears between
# them in raw input order.
"$gnuld" -melf64lriscv -static --gc-sections --sort-section=alignment -e _start \
  -o "$work/gnu.elf" "$work/caller.o" "$work/filler.o" "$work/target.o"
set +e
"$qemu" "$work/gnu.elf"
gnu_rc=$?
set -e
test "$gnu_rc" -eq 53

echo "MINILD_GC_ALIGNMENT_GNU=PASS qemu_rc=53"

set +e
"$minild" -melf64lriscv -static --gc-sections -e _start \
  -o "$work/mini.elf" "$work/caller.o" "$work/filler.o" "$work/target.o" \
  >"$work/minild.stdout" 2>"$work/minild.stderr"
mini_rc=$?
set -e

if test "$mini_rc" -eq 0; then
  set +e
  "$qemu" "$work/mini.elf"
  mini_qemu_rc=$?
  set -e
  if test "$mini_qemu_rc" -eq 53; then
    echo "MINILD_GC_ALIGNMENT_V0=PASS qemu_rc=53"
    exit 0
  fi
fi

grep -q 'R_RISCV_JAL-overflow' "$work/minild.stderr"
echo "MINILD_GC_ALIGNMENT_V0=REPRODUCED mini_rc=$mini_rc expected=R_RISCV_JAL-overflow"
exit 1
