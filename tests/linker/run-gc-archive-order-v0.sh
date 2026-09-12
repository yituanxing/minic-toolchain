#!/usr/bin/env bash
set -Eeuo pipefail

build="${BUILD_DIR:-build/minild-gc-archive-order-v0}"
minild="${MINILD:-$build/bin/minic-ld}"
as="${RISCV_AS:-riscv64-linux-gnu-as}"
ar="${RISCV_AR:-riscv64-linux-gnu-ar}"
gnuld="${RISCV_LD:-riscv64-linux-gnu-ld}"
qemu="${QEMU_RISCV64:-qemu-riscv64}"
work="$build/gc-archive-order-v0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/start.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    call archive_target
    li a7, 93
    ecall
.size _start, .-_start
S

# This member is first in the archive, but is not needed until archive_target
# has been extracted from the later member.  It contains the range-sensitive
# direct JAL back to archive_target.
cat >"$work/early.s" <<'S'
.text
.globl archive_early
.type archive_early, @function
archive_early:
    j archive_target
.size archive_early, .-archive_early
S

cat >"$work/target.s" <<'S'
.text
.globl archive_target
.type archive_target, @function
archive_target:
    la t0, archive_early
    call archive_filler
    li a0, 43
    ret
.size archive_target, .-archive_target
S

# Keep a >1 MiB live input section after target.  If the linker lays selected
# archive members in lazy-extraction order (target, filler, early), the direct
# JAL in early becomes invalid.  Archive order (early, target, filler) keeps it
# near, which is the behavior frozen by the GNU reference below.
cat >"$work/filler.s" <<'S'
.text
.globl archive_filler
.type archive_filler, @function
archive_filler:
    ret
    .space 2097152, 0
.size archive_filler, .-archive_filler
S

"$as" -march=rv64gc -mabi=lp64d -o "$work/start.o" "$work/start.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/early.o" "$work/early.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/target.o" "$work/target.s"
"$as" -march=rv64gc -mabi=lp64d -o "$work/filler.o" "$work/filler.s"
"$ar" crs "$work/liborder.a" "$work/early.o" "$work/target.o" "$work/filler.o"

"$gnuld" -melf64lriscv -static --gc-sections -e _start \
  -o "$work/gnu.elf" "$work/start.o" "$work/liborder.a"
set +e
"$qemu" "$work/gnu.elf"
gnu_rc=$?
set -e
test "$gnu_rc" -eq 43

echo "MINILD_GC_ARCHIVE_ORDER_GNU=PASS qemu_rc=43"

"$minild" -melf64lriscv -static --gc-sections -e _start \
  -o "$work/mini.elf" "$work/start.o" "$work/liborder.a" \
  >"$work/minild.stdout" 2>"$work/minild.stderr"
set +e
"$qemu" "$work/mini.elf"
mini_rc=$?
set -e
test "$mini_rc" -eq 43

echo "MINILD_GC_ARCHIVE_ORDER_V0=PASS archive_order=preserved qemu_rc=43"
