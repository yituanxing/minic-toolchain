#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${MINIAS:?MINIAS is required}"
: "${MINILD:?MINILD is required}"

QEMU="${QEMU_RISCV64:-qemu-riscv64}"
READELF="${RISCV_READELF:-riscv64-linux-gnu-readelf}"
WORK="${BUILD_DIR:-build}/linker-gc-v0"

rm -rf "$WORK"
mkdir -p "$WORK"

cat >"$WORK/gc.c" <<'C'
extern void minic_dead_missing_symbol(void);

static void minic_dead_function(void)
{
    minic_dead_missing_symbol();
}

static int minic_live_helper(void)
{
    return 23;
}

int minic_live_function(void)
{
    return minic_live_helper();
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
grep -q '\.text\.minic_live_helper' "$WORK/gc.sections"
grep -q '\.text\.minic_live_function' "$WORK/gc.sections"

# Without GC, the dead function's undefined edge must remain observable.
set +e
"$MINILD" -melf64lriscv -static -e _start \
  -o "$WORK/no-gc.elf" "$WORK/start.o" "$WORK/gc.o" \
  >"$WORK/no-gc.stdout" 2>"$WORK/no-gc.stderr"
no_gc_rc=$?
set -e
test "$no_gc_rc" -ne 0
grep -q 'minic_dead_missing_symbol' "$WORK/no-gc.stderr"

# With GC, _start -> live_function -> live_helper stays reachable while the
# dead function and its undefined relocation are discarded.
"$MINILD" -melf64lriscv -static --gc-sections -e _start \
  -o "$WORK/gc.elf" "$WORK/start.o" "$WORK/gc.o" \
  >"$WORK/gc.stdout" 2>"$WORK/gc.stderr"

test -s "$WORK/gc.elf"
set +e
"$QEMU" "$WORK/gc.elf"
qemu_rc=$?
set -e
test "$qemu_rc" -eq 23

if "$READELF" -Ws "$WORK/gc.elf" | grep -q 'minic_dead_function'; then
  echo "MINILD_GC_ERROR dead function survived" >&2
  exit 1
fi
"$READELF" -Ws "$WORK/gc.elf" | grep -q 'minic_live_function'
"$READELF" -Ws "$WORK/gc.elf" | grep -q 'minic_live_helper'

echo "MINILD_GC_V0=PASS dead=discarded transitive=retained qemu_rc=23"
