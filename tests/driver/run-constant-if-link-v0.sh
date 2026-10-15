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

# Negative controls for CORE_ZERO_ONLY_BOOL_CFG_V1: the optimizer must never
# erase externally observable calls when a bool can become nonzero, escapes
# through a pointer, or is volatile. Keep symbols unresolved deliberately and
# inspect the native MiniC object, independent of ELF section GC.
cat >"$WORK/zero-bool-negative.c" <<'C_NEGATIVE'
extern int must_keep_dynamic_bool(void);
extern int must_keep_aliased_bool(void);
extern int must_keep_volatile_bool(void);
extern int must_keep_nonzero_bool(void);
int keep_dynamic_bool(int condition) {
    _Bool flag = 0;
    if (condition) flag = 1;
    if (flag) return must_keep_dynamic_bool();
    return 0;
}
int keep_aliased_bool(int condition) {
    _Bool flag = 0;
    _Bool *alias = &flag;
    if (condition) *alias = 1;
    if (flag) return must_keep_aliased_bool();
    return 0;
}
int keep_volatile_bool(int condition) {
    volatile _Bool flag = 0;
    if (condition) flag = 1;
    if (flag) return must_keep_volatile_bool();
    return 0;
}
int keep_nonzero_bool(void) {
    _Bool flag = 1;
    if (flag) return must_keep_nonzero_bool();
    return 0;
}
C_NEGATIVE
"$MINIC" -ffreestanding -fno-builtin -nostdinc -c \
  "$WORK/zero-bool-negative.c" -o "$WORK/zero-bool-negative.o"
"${RISCV_NM:-riscv64-linux-gnu-nm}" -u "$WORK/zero-bool-negative.o" \
  >"$WORK/zero-bool-negative.undefined"
for symbol in must_keep_dynamic_bool must_keep_aliased_bool \
              must_keep_volatile_bool must_keep_nonzero_bool; do
    if ! grep -Eq "[[:space:]]U[[:space:]]$symbol$" "$WORK/zero-bool-negative.undefined"; then
        echo "MINIC_ZERO_BOOL_NEGATIVE=FAIL erased_live_ref=$symbol" >&2
        cat "$WORK/zero-bool-negative.undefined" >&2
        exit 1
    fi
done
echo "MINIC_ZERO_BOOL_NEGATIVE=PASS dynamic=1 alias=1 volatile=1 nonzero=1"

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
