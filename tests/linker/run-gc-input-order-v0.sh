#!/usr/bin/env bash
set -Eeuo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${BUILD_DIR:-$root/build/minild-gc-input-order-v0}"
minild="${MINILD:-$build/bin/minic-ld}"
as="${RISCV_AS:-riscv64-linux-gnu-as}"
qemu="${QEMU_RISCV64:-qemu-riscv64}"
work="$build/gc-input-order-v0"
mkdir -p "$work"

cat >"$work/caller.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    j target
.size _start, .-_start
S

cat >"$work/target.s" <<'S'
.section .text.target,"ax",@progbits
.globl target
.type target, @function
target:
    call live_filler
    li a0, 37
    li a7, 93
    ecall
.size target, .-target
S

cat >"$work/filler.s" <<'S'
.section .text.filler,"ax",@progbits
.globl live_filler
.type live_filler, @function
live_filler:
    ret
    .space 2097152, 0
.size live_filler, .-live_filler
S

cat >"$work/tail.s" <<'S'
.text
.globl dead_plain_tail
.type dead_plain_tail, @function
dead_plain_tail:
    ret
.size dead_plain_tail, .-dead_plain_tail
S

"$as" -march=rv64gc -mabi=lp64d -o "$work/caller.o" "$work/caller.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/target.o" "$work/target.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/filler.o" "$work/filler.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/tail.o" "$work/tail.s"

"$minild" -melf64lriscv -static --gc-sections -e _start \
  -o "$work/order.elf" \
  "$work/caller.o" "$work/target.o" "$work/filler.o" "$work/tail.o" \
  >"$work/minild.stdout" 2>"$work/minild.stderr"
grep -q 'gc-input-order:' "$work/minild.stderr"

set +e
"$qemu" "$work/order.elf"
rc=$?
set -e
test "$rc" -eq 37
echo "MINILD_GC_INPUT_ORDER_V0=PASS jal=near-after-restore qemu_rc=37"
