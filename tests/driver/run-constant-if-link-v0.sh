#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${MINIAS:?MINIAS is required}"
LD="${RISCV_LD:-riscv64-linux-gnu-ld}"
QEMU="${QEMU_RISCV64:-qemu-riscv64}"
WORK="${BUILD_DIR:-build}/constant-if-link-v0"

rm -rf "$WORK"
mkdir -p "$WORK"

cat >"$WORK/probe.c" <<'C'
extern int missing_if_zero(void);
extern int missing_sizeof(void);
extern int missing_else(void);

int live_probe(void)
{
    if (0)
        return missing_if_zero();
    if (sizeof(unsigned) == 8)
        return missing_sizeof();
    if (1)
        return 23;
    else
        return missing_else();
}
C

cat >"$WORK/start.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    call live_probe
    li a7, 93
    ecall
.size _start, .-_start
S

"$MINIC" -ffreestanding -fno-builtin -nostdinc -c "$WORK/probe.c" -o "$WORK/probe.o"
"$MINIAS" -march=rv64gc -mabi=lp64d -o "$WORK/start.o" "$WORK/start.s"

# All three undefined calls are inside the live function itself.  Function-level
# section GC cannot help: this link succeeds only if Core did not emit the
# compile-time-dead if branches.
"$LD" -melf64lriscv -static --gc-sections -e _start \
  -o "$WORK/probe.elf" "$WORK/start.o" "$WORK/probe.o"

set +e
"$QEMU" "$WORK/probe.elf"
rc=$?
set -e
test "$rc" -eq 23

echo "MINIC_CONSTANT_IF_LINK_V0=PASS dead-refs=3 qemu_rc=23"
