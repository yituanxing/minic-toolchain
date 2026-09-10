#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${MINIAS:?MINIAS is required}"

LD="${RISCV_LD:-riscv64-linux-gnu-ld}"
READELF="${RISCV_READELF:-riscv64-linux-gnu-readelf}"
QEMU="${QEMU_RISCV64:-qemu-riscv64}"
WORK="${BUILD_DIR:-build}/driver-section-gc-v0"

rm -rf "$WORK"
mkdir -p "$WORK"

cat >"$WORK/gc.c" <<'C'
extern void minic_dead_missing_symbol(void);

static void minic_dead_function(void)
{
    minic_dead_missing_symbol();
}

int minic_live_function(void)
{
    return 23;
}
C

cat >"$WORK/start.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    call minic_live_function
    li a7, 93
    ecall
.size _start, .-_start
S

"$MINIC" -ffreestanding -fno-builtin -nostdinc \
  -c "$WORK/gc.c" -o "$WORK/gc.o"
"$MINIAS" -march=rv64gc -mabi=lp64d \
  -o "$WORK/start.o" "$WORK/start.s"

"$READELF" -SW "$WORK/gc.o" >"$WORK/gc.sections"
grep -q '\.text\.minic_dead_function' "$WORK/gc.sections"
grep -q '\.text\.minic_live_function' "$WORK/gc.sections"

# The dead function deliberately references an undefined symbol.  This link
# succeeds only if the dead function is a separately collectable input section.
"$LD" -melf64lriscv -static --gc-sections -e _start \
  -o "$WORK/gc.elf" "$WORK/start.o" "$WORK/gc.o"

set +e
"$QEMU" "$WORK/gc.elf"
rc=$?
set -e
test "$rc" -eq 23

echo "MINIC_DRIVER_SECTION_GC_V0=PASS sections=per-function dead-undefined=collected qemu_rc=23"
