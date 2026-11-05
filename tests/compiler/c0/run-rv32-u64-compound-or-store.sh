#!/usr/bin/env bash
# Strict RV32 ELF32 GNU-C-vs-MiniC-C runtime differential for the Linux 6.6.143
# vp_modern_get_features() high feature-register compound-assignment writeback.
# A missing GNU cross tool or actual qemu-riscv32 is a FAIL, never a SKIP/PASS.
set -Eeuo pipefail

: "${MINIC_CC:?Set MINIC_CC to the executable MiniC compiler}"
test -x "$MINIC_CC" || {
  echo "RV32_VIRTIO_FEATURE=FAIL cause=minic_compiler_unavailable" >&2
  exit 127
}
for tool in riscv64-linux-gnu-gcc riscv64-linux-gnu-ld qemu-riscv32 timeout; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "RV32_VIRTIO_FEATURE=FAIL cause=missing_tool tool=$tool" >&2
    exit 127
  }
done

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Freeze one GNU -E output and use it for both C compilers; no separate
# preprocessing interpretation can contaminate the differential.
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -E -P -std=gnu11 \
  -ffreestanding -fno-builtin -x c \
  "$root/tests/compiler/c0/rv32_u64_compound_or_store.c" \
  -o "$work/virtio.i"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/virtio.i" -o "$work/virtio.minic.s"
# The GNU freestanding ELF32 fixture has no compiler-rt/libgcc closure.
# Base-I two-word shifts must be real typed instruction sequences here,
# otherwise the GNU LD/QEMU differential fails before examining behavior.
if grep -Eq '^[[:space:]]+call[[:space:]]+__(ashldi3|ashrdi3|lshrdi3)' \
    "$work/virtio.minic.s"; then
  echo 'RV32_VIRTIO_FEATURE=FAIL cause=wide_shift_libcall_without_runtime' >&2
  exit 1
fi
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
  -c "$work/virtio.minic.s" -o "$work/virtio.minic.o"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O1 -std=gnu11 \
  -ffreestanding -fno-builtin -fno-stack-protector -x c \
  -c "$work/virtio.i" -o "$work/virtio.gnu.o"

cat > "$work/driver.c" <<'SRC'
typedef unsigned int U32;
typedef unsigned long long U64;
typedef U64 (*FeatureFn)(U32, U32);
extern U64 rv32_virtio_feature_rmw(U32, U32);
extern U64 rv32_virtio_feature_explicit(U32, U32);
extern U64 rv32_virtio_feature_indirect(FeatureFn, U32, U32);
extern int rv32_virtio_has_version_1(U32, U32);
extern U64 rv32_virtio_negotiated_transport(U32, U32);

/* Independent GNU-built callback, so the MiniC-built indirect caller must
 * correctly retain a0/a1 from a non-MiniC function. */
static U64 gnu_features(U32 low, U32 high) {
    return (((U64)high) << 32) | low;
}
struct Case { U32 low, high; };
static const struct Case cases[] = {
    {0x00000000U, 0x00000000U}, /* negative: version bit really absent */
    {0x00000001U, 0x00000001U}, /* crucial: version bit 32 present */
    {0x87654321U, 0x00000001U}, /* preserve nonzero low while setting bit32 */
    {0x12345678U, 0x00000002U}, /* negative: high32 nonzero, bit32 absent */
    {0xffffffffU, 0x80000001U}, /* carry uppermost bit and version bit */
    {0x00000000U, 0xffffffffU}, /* all high bits, zero low */
    {0x00000001U, 0x80000000U}, /* uppermost bit without version bit */
    {0xf0000000U, 0x00000003U}, /* variable shifts across bits 28..33 */
};

int main(void) {
    unsigned i;
    /* GNU-built indirect caller -> MiniC function, from a volatile callback
     * pointer to ensure an actual indirect call at runtime. */
    FeatureFn volatile minic_callback = rv32_virtio_feature_rmw;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        U32 low = cases[i].low;
        U32 high = cases[i].high;
        U64 expected = gnu_features(low, high);
        U64 transport_mask = 0;
        int version_1 = (int)(high & 1U);
        unsigned bit;
        for (bit = 28; bit < 41; ++bit)
            transport_mask |= 1ULL << bit;
        if (rv32_virtio_feature_rmw(low, high) != expected) return 11;
        if (rv32_virtio_feature_explicit(low, high) != expected) return 12;
        if (minic_callback(low, high) != expected) return 13;
        if (rv32_virtio_feature_indirect(gnu_features, low, high) != expected)
            return 14;
        if (rv32_virtio_has_version_1(low, high) != version_1) return 15;
        if (rv32_virtio_negotiated_transport(low, high) !=
            (expected & transport_mask)) return 16;
    }
    return 0;
}
SRC

cat > "$work/start.s" <<'SRC'
.text
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  call main
  li a7, 93
  ecall
SRC

riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O1 -std=gnu11 \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -c "$work/driver.c" -o "$work/driver.o"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
  -c "$work/start.s" -o "$work/start.o"

for mode in gnu minic; do
  riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/$mode.elf" "$work/start.o" "$work/driver.o" \
    "$work/virtio.$mode.o"
  set +e
  timeout 30s qemu-riscv32 "$work/$mode.elf"
  qemu_rc=$?
  set -e
  if [[ "$qemu_rc" -ne 0 ]]; then
    echo "RV32_VIRTIO_FEATURE=FAIL mode=$mode cause=real_qemu_elf32_runtime qemu_exit=$qemu_rc" >&2
    exit 1
  fi
  echo "RV32_VIRTIO_FEATURE_MODE=PASS mode=$mode GNU_AS=1 GNU_LD=1 ELF32=1 QEMU=1"
done
echo 'RV32_VIRTIO_FEATURE=PASS cases=8 version1_positive_and_negative=1 transport_bits_28_to_40=1 compound_or_writeback=1 u64_return_a0_a1=1 indirect_call_both_directions=1 GNU_C_oracle=1 MiniC_C=1 GNU_AS_LD=1 QEMU=1'
