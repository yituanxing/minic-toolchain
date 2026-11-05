#!/usr/bin/env bash
# RV32I 64-bit nonzero -> C _Bool: frozen VirtIO VERSION_1 (bit 32) regression.
# GNU C and MiniC share the same preprocessed TU; GNU AS, MiniAS, GNU ld, QEMU
# must all agree on ABI, ELF32 and each guest assertion. No libc or Linux rebuild.
set -Eeuo pipefail
: "$MINIC_CC" "$MINIAS"
test -x "$MINIC_CC"
test -x "$MINIAS"
for tool in riscv64-linux-gnu-gcc riscv64-linux-gnu-ld \
            riscv64-linux-gnu-readelf qemu-riscv32 timeout; do
    command -v "$tool" >/dev/null
done
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

cat >"$scratch/bit64-bool.c" <<'SRC'
typedef unsigned long long U64;
typedef _Bool Bool;

/* Match the original Linux virtio_device.features field offset on RV32. */
struct VDevice {
    unsigned char prefix[608];
    U64 features;
};
/* Original Linux virtio_config.h conversion: U64 expression returned as bool. */
static inline Bool virtio_test_bit(const struct VDevice *vdev,
                                  unsigned int fbit) {
    return vdev->features & (1ULL << fbit);
}
__attribute__((noinline)) Bool implicit_return(U64 value) {
    return value;
}
__attribute__((noinline)) Bool implicit_assignment(U64 value) {
    Bool selected = value;
    return selected;
}
__attribute__((noinline)) Bool explicit_cast(U64 value) {
    return (Bool)value;
}
__attribute__((noinline)) int if_condition(U64 value) {
    if (value) return 17;
    return 29;
}
__attribute__((noinline)) int ternary_condition(U64 value) {
    return value ? 19 : 31;
}
__attribute__((noinline)) Bool double_negation(U64 value) {
    return !!value;
}
__attribute__((noinline)) Bool invert_then_invert(U64 value) {
    return !(!value);
}
int main(void) {
    struct VDevice dev;
    U64 zero = 0ULL;
    U64 low = 1ULL;
    U64 bit32 = 1ULL << 32;
    U64 bit63 = 1ULL << 63;
    U64 both = bit32 | bit63;
    if (implicit_return(zero) || !implicit_return(low)) return 11;
    if (!implicit_return(bit32) || !implicit_return(bit63)) return 12;
    if (!implicit_assignment(bit32) || !implicit_assignment(bit63)) return 13;
    if (implicit_assignment(zero) || !explicit_cast(bit32) ||
        !explicit_cast(bit63)) return 14;
    if (explicit_cast(zero) || explicit_cast(0ULL)) return 15;
    if (if_condition(bit32) != 17 || if_condition(bit63) != 17 ||
        if_condition(zero) != 29) return 16;
    if (ternary_condition(bit32) != 19 || ternary_condition(bit63) != 19 ||
        ternary_condition(zero) != 31) return 17;
    if (!double_negation(bit32) || !double_negation(bit63) ||
        double_negation(zero)) return 18;
    if (!invert_then_invert(bit32) || !invert_then_invert(bit63) ||
        invert_then_invert(zero)) return 19;

    dev.features = bit32;
    if (!virtio_test_bit(&dev,32U) || virtio_test_bit(&dev,63U) ||
        virtio_test_bit(&dev,0U) || virtio_test_bit(&dev,31U)) return 21;
    dev.features = bit63;
    if (!virtio_test_bit(&dev,63U) || virtio_test_bit(&dev,32U) ||
        virtio_test_bit(&dev,62U)) return 22;
    dev.features = both;
    if (!virtio_test_bit(&dev,32U) || !virtio_test_bit(&dev,63U) ||
        virtio_test_bit(&dev,31U) || virtio_test_bit(&dev,62U)) return 23;
    dev.features = zero;
    if (virtio_test_bit(&dev,32U) || virtio_test_bit(&dev,63U)) return 24;
    return 0;
}
SRC

cat >"$scratch/start.s" <<'SRC'
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

riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
    -c "$scratch/start.s" -o "$scratch/start.o"
riscv64-linux-gnu-gcc -E -P -x c \
    "$scratch/bit64-bool.c" -o "$scratch/bit64-bool.i"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$scratch/bit64-bool.i" -o "$scratch/bit64-bool.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -x assembler -c \
    "$scratch/bit64-bool.s" -o "$scratch/minic-gas.o"
"$MINIAS" -march=rv32i -mabi=ilp32 \
    -o "$scratch/minic-as.o" "$scratch/bit64-bool.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -std=gnu11 \
    -O1 -fno-inline -ffreestanding -fno-builtin -fno-stack-protector -fno-pic \
    -c "$scratch/bit64-bool.i" -o "$scratch/gnu.o"

for producer in gnu minic-gas minic-as; do
    object="$scratch/$producer.o"
    elf="$scratch/$producer.elf"
    riscv64-linux-gnu-readelf -h "$object" | grep -Eq 'Class:[[:space:]]+ELF32'
    riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
        -o "$elf" "$scratch/start.o" "$object"
    riscv64-linux-gnu-readelf -h "$elf" | grep -Eq 'Class:[[:space:]]+ELF32'
    riscv64-linux-gnu-readelf -h "$elf" | grep -Eq 'Machine:[[:space:]]+RISC-V'
    status=0
    timeout 30s qemu-riscv32 "$elf" || status=$?
    if test "$status" -ne 0; then
        echo "RV32_U64_BOOL_QEMU=FAIL producer=$producer exit=$status" >&2
        exit 1
    fi
    echo "RV32_U64_BOOL_CASE=PASS producer=$producer elf=ELF32 march=rv32i mabi=ilp32"
done
echo 'RV32_U64_BOOL_QEMU=PASS bits=0,32,63 contexts=return,assign,cast,if,ternary,logical,virtio GNU_C MiniC_GNU_AS MiniC_MiniAS'
