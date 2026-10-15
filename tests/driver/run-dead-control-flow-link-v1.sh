#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${MINIAS:?MINIAS is required}"
LD="${RISCV_LD:-riscv64-linux-gnu-ld}"
NM="${RISCV_NM:-riscv64-linux-gnu-nm}"
QEMU="${QEMU_RISCV64:-qemu-riscv64}"
WORK="${BUILD_DIR:-build}/dead-control-flow-link-v1"

rm -rf "$WORK"
mkdir -p "$WORK"

cat >"$WORK/probe.c" <<'C'
extern int missing_dead_and(void);
extern int missing_dead_or(void);
extern int missing_dead_loop(void);

int live_probe(int live)
{
    if (live && (0 && missing_dead_and()))
        return 101;
    if (live || (1 || missing_dead_or()))
        live = 7;
    if (0) {
        int i;
        for (i = 0; i < 3; ++i)
            live += missing_dead_loop();
    }
    return live + 16;
}
C

cat >"$WORK/start.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    li a0, 0
    call live_probe
    li a7, 93
    ecall
.size _start, .-_start
S

"$MINIC" -ffreestanding -fno-builtin -nostdinc -c "$WORK/probe.c" -o "$WORK/probe.o"
"$MINIAS" -march=rv64gc -mabi=lp64d -o "$WORK/start.o" "$WORK/start.s"

# The missing symbols are deliberately undefined. They occur only in source
# regions proven unreachable by C short-circuit/constant-control-flow rules.
# Any emitted relocation makes this regression fail before runtime.
if "$NM" -u "$WORK/probe.o" | grep -E 'missing_dead_(and|or|loop)' >"$WORK/dead-refs.txt"; then
    cat "$WORK/dead-refs.txt" >&2
    exit 1
fi

"$LD" -melf64lriscv -static --gc-sections -e _start \
  -o "$WORK/probe.elf" "$WORK/start.o" "$WORK/probe.o"

set +e
"$QEMU" "$WORK/probe.elf"
rc=$?
set -e
test "$rc" -eq 23

echo "MINIC_DEAD_CONTROL_FLOW_LINK_V1=PASS dead-refs=3 qemu_rc=23"
