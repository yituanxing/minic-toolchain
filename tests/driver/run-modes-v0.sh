#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${MINIAS:?MINIAS is required}"

NM="${RISCV_NM:-riscv64-linux-gnu-nm}"
READELF="${RISCV_READELF:-riscv64-linux-gnu-readelf}"
QEMU="${QEMU_RISCV64:-qemu-riscv64}"
WORK="${BUILD_DIR:-build}/driver-modes-v0"

rm -rf "$WORK"
mkdir -p "$WORK"

cat >"$WORK/mode.c" <<'C'
#ifndef __STDC_HOSTED__
#error __STDC_HOSTED__ must be defined
#endif

#if __STDC_HOSTED__ == 1
int minic_hosted_mode_marker = 1;
#elif __STDC_HOSTED__ == 0
int minic_freestanding_mode_marker = 1;
#else
#error unexpected __STDC_HOSTED__
#endif

int mode_value(void)
{
    return __STDC_HOSTED__;
}
C

"$MINIC" -c "$WORK/mode.c" -o "$WORK/hosted.o"
"$MINIC" -ffreestanding -c "$WORK/mode.c" -o "$WORK/freestanding.o"
"$MINIC" -ffreestanding -fhosted -c "$WORK/mode.c" -o "$WORK/hosted-explicit.o"

"$NM" "$WORK/hosted.o" >"$WORK/hosted.nm"
"$NM" "$WORK/freestanding.o" >"$WORK/freestanding.nm"
"$NM" "$WORK/hosted-explicit.o" >"$WORK/hosted-explicit.nm"

grep -q ' minic_hosted_mode_marker$' "$WORK/hosted.nm"
grep -q ' minic_freestanding_mode_marker$' "$WORK/freestanding.nm"
grep -q ' minic_hosted_mode_marker$' "$WORK/hosted-explicit.nm"

cat >"$WORK/free.c" <<'C'
int free_value(void)
{
    return 42;
}
C

cat >"$WORK/start.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    call free_value
    li a7, 93
    ecall
.size _start, .-_start
S

"$MINIC" -ffreestanding -fno-builtin -nostdinc   -c "$WORK/free.c" -o "$WORK/free.o"
"$MINIAS" -march=rv64gc -mabi=lp64d   -o "$WORK/start.o" "$WORK/start.s"

"$MINIC" -ffreestanding -nostdlib -static   -o "$WORK/free-nostdlib.elf" "$WORK/start.o" "$WORK/free.o"
"$MINIC" -ffreestanding -nostartfiles -nodefaultlibs -static   -o "$WORK/free-split.elf" "$WORK/start.o" "$WORK/free.o"

"$READELF" -h "$WORK/free-nostdlib.elf" >"$WORK/free.header"
"$READELF" -lW "$WORK/free-nostdlib.elf" >"$WORK/free.programs"
if grep -q 'Requesting program interpreter' "$WORK/free.programs"; then
    echo "MINIC_FREESTANDING_DRIVER=FAIL unexpected-interpreter" >&2
    exit 1
fi
if "$READELF" -d "$WORK/free-nostdlib.elf" 2>/dev/null | grep -q '(NEEDED)'; then
    echo "MINIC_FREESTANDING_DRIVER=FAIL unexpected-needed" >&2
    exit 1
fi

set +e
"$QEMU" "$WORK/free-nostdlib.elf"
rc_nostdlib=$?
"$QEMU" "$WORK/free-split.elf"
rc_split=$?
set -e

test "$rc_nostdlib" -eq 42
test "$rc_split" -eq 42
cmp "$WORK/free-nostdlib.elf" "$WORK/free-split.elf"

echo "MINIC_DRIVER_MODES_V0=PASS hosted=1 freestanding=0 nostdlib=PASS split_runtime_flags=PASS qemu_rc=42"
