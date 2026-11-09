#!/usr/bin/env bash
# Compile-only privileged RISC-V CSR source-ASM contract. The C compiler
# emits only base-I selected instructions; CSRR/CSRRC/CSRS are exact GNU
# source templates and must retain source provenance. Never execute these
# supervisor-mode instructions in user-mode QEMU.
set -Eeuo pipefail
: "${MINIC_CC:?MINIC_CC must point at minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/csr.i" <<'SRC'
unsigned long linux_read_sstatus(void) {
    unsigned long result;
    __asm__ __volatile__("csrr %0, 0x100" : "=r"(result) : : "memory");
    return result;
}
unsigned long linux_irq_save(unsigned long mask) {
    unsigned long old;
    __asm__ __volatile__("csrrc %0, 0x100, %1"
                         : "=r"(old) : "rK"(mask) : "memory");
    return old;
}
unsigned long linux_early_output(void) {
    unsigned long old;
    __asm__ __volatile__("csrr %0, 0x100" : "=&r"(old) : : "memory");
    return old;
}
unsigned long linux_register_rmw(unsigned long value) {
    __asm__ __volatile__("addi %0, %0, 1" : "+r"(value) : : "memory");
    return value;
}
void linux_irq_restore(unsigned long mask) {
    __asm__ __volatile__("csrs 0x100, %0" : : "rK"(mask) : "memory");
}
SRC

for width in 32 64; do
    if [[ $width == 32 ]]; then
        target=riscv32-linux march=rv32i abi=ilp32 marker=MINIC_SOURCE_INLINE_ASM
    else
        target=riscv64-linux march=rv64i abi=lp64 marker=MINIC_RV64I_ASM
    fi
    output="$work/$target.s"
    "$MINIC_CC" --target="$target" -march="$march" -mabi="$abi" \
        -S "$work/csr.i" -o "$output"
    for symbol in linux_read_sstatus linux_irq_save linux_early_output \
                  linux_register_rmw linux_irq_restore; do
        grep -Fq "$symbol:" "$output"
    done
    grep -Fq 'csrr x5, 0x100' "$output"
    grep -Fq 'csrrc x5, 0x100, x6' "$output"
    grep -Fq 'addi x5, x5, 1' "$output"
    grep -Fq 'csrs 0x100, x10' "$output"
    grep -Fq "# ${marker}_BEGIN" "$output"
    grep -Fq "# ${marker}_END" "$output"
    # The lvalue writeback must cover every byte; no alignment speculation.
    grep -Fq 'sb x6, 0(x7)' "$output"
    if [[ $width == 32 ]]; then
        grep -Fq 'sb x6, 3(x7)' "$output"
    else
        grep -Fq 'sb x6, 7(x7)' "$output"
    fi
    echo "RISCV_TYPED_CSR=PASS target=$target outputs=3 rmw=1 scalar_inputs=2 provenance=1 byte_writeback=1"
done

cat >"$work/memory-output.i" <<'SRC'
unsigned long invalid_memory_csr(void) {
    unsigned long value;
    __asm__ __volatile__("csrr %0, 0x100" : "=m"(value) : : "memory");
    return value;
}
SRC
cat >"$work/conflict-clobber.i" <<'SRC'
unsigned long invalid_asm_clobber(void) {
    unsigned long value;
    __asm__ __volatile__("csrr %0, 0x100" : "=r"(value) : : "t0", "memory");
    return value;
}
SRC
for width in 32 64; do
    if [[ $width == 32 ]]; then
        target=riscv32-linux march=rv32i abi=ilp32
    else
        target=riscv64-linux march=rv64i abi=lp64
    fi
    for bad in memory-output conflict-clobber; do
        output="$work/$target-$bad.s"
        if "$MINIC_CC" --target="$target" -march="$march" -mabi="$abi" \
            -S "$work/$bad.i" -o "$output" >"$work/$bad.stdout" \
            2>"$work/$bad.stderr"; then
            echo "RISCV_TYPED_CSR=FAIL accepted_bad=$bad target=$target" >&2
            exit 1
        fi
        test ! -e "$output"
    done
    echo "RISCV_TYPED_CSR_NEGATIVE=PASS target=$target unsupported_memory=1 clobber_conflict=1"
done
