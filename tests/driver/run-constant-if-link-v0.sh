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
extern int missing_arch_dma_set_uncached(void);

/* Based on the real Linux 6.6.143 kernel/dma/direct.o LINK_FAIL:
   CONFIG_ARCH_HAS_DMA_SET_UNCACHED is disabled, so a mutable local
   initialized only to zero must never cause a live external call. */
static int linux_dma_set_uncached_disabled(int coherent)
{
    _Bool set_uncached = 0, remap = 0;
    if (!coherent) {
        set_uncached = 0;
        remap = 0;
        if (!set_uncached && !remap)
            return 17;
    }
    if (set_uncached)
        return missing_arch_dma_set_uncached();
    return 23;
}

int live_probe(void)
{
    if (0)
        return missing_if_zero();
    if (sizeof(unsigned) == 8)
        return missing_sizeof();
    if (1)
        return linux_dma_set_uncached_disabled(1);
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

# The three original undefined calls plus the new DMA local-bool call are
# inside active function paths. Function-level
# section GC cannot help: this link succeeds only if Core did not emit the
# compile-time-dead if branches.
"$LD" -melf64lriscv -static --gc-sections -e _start \
  -o "$WORK/probe.elf" "$WORK/start.o" "$WORK/probe.o"

set +e
"$QEMU" "$WORK/probe.elf"
rc=$?
set -e
test "$rc" -eq 23

echo "MINIC_CONSTANT_IF_LINK_V0=PASS dead-refs=4 dma_bool_cfg_false=PASS qemu_rc=23"
