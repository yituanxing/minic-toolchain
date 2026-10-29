#!/usr/bin/env bash
# The four CodeEmitters contribute bytes and relocations to one Common-owned
# fragment transaction. This oracle needs only MiniAS and host readelf:
# it deliberately does not infer GNU object equivalence or Linux acceptance.
set -Eeuo pipefail
: "${MINIAS:=build/release/bin/minic-as}"
test -x "$MINIAS"
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/x86_64.s" <<'ASM'
.cfi_sections .debug_frame
.text
.globl transaction_x86
transaction_x86:
.cfi_startproc
    call external_x86
    nop
.cfi_endproc
.reloc 0, R_X86_64_PC32, explicit_x86+5
ASM

cat >"$work/riscv.s" <<'ASM'
.text
.globl transaction_rv
transaction_rv:
    la a0, external_rv
    call external_rv_call
ASM

cat >"$work/aarch64.s" <<'ASM'
.text
.globl transaction_a64
transaction_a64:
    bl external_a64_branch
    adrp x0, external_a64_data
    add x0, x0, :lo12:external_a64_data
ASM

cat >"$work/loongarch64.s" <<'ASM'
.text
.globl transaction_loong
transaction_loong:
    bl external_loong_branch
    la.pcrel $a0, external_loong_data
ASM

for target in x86_64 riscv aarch64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target.o" "$work/$target.s"
    readelf -SW "$work/$target.o" >"$work/$target.sections"
    readelf -rW "$work/$target.o" >"$work/$target.relocs"
    readelf -sW "$work/$target.o" >"$work/$target.symbols"
    grep -Eq '[[:space:]]+\.text[[:space:]]+PROGBITS[[:space:]]+' \
        "$work/$target.sections"
    grep -Eq 'transaction_(x86|rv|a64|loong)' "$work/$target.symbols"
done

# Instruction-owned, explicit .reloc and synthesized CFI section relocations
# must coexist; no producer may silently absorb one into another.
grep -q 'external_x86' "$work/x86_64.relocs"
grep -q 'explicit_x86' "$work/x86_64.relocs"
grep -q 'Relocation section.*\.rela.debug_frame' "$work/x86_64.relocs"
grep -q 'external_rv' "$work/riscv.relocs"
grep -q 'R_RISCV_PCREL_HI20' "$work/riscv.relocs"
grep -q 'R_RISCV_PCREL_LO12_I' "$work/riscv.relocs"
grep -q 'R_RISCV_CALL_PLT' "$work/riscv.relocs"
grep -q 'external_a64_branch' "$work/aarch64.relocs"
grep -q 'external_a64_data' "$work/aarch64.relocs"
grep -q 'R_AARCH64_ADR_PREL_PG_HI21' "$work/aarch64.relocs"
grep -q 'R_AARCH64_ADD_ABS_LO12_NC' "$work/aarch64.relocs"
grep -q 'external_loong_branch' "$work/loongarch64.relocs"
grep -q 'external_loong_data' "$work/loongarch64.relocs"
grep -q 'R_LARCH_PCALA_HI20' "$work/loongarch64.relocs"
grep -q 'R_LARCH_PCALA_LO12' "$work/loongarch64.relocs"

# A late invalid statement after an otherwise valid instruction+relocation
# sequence must not leave an object file (even if output was requested).
# This tests public fail-closed behavior, not allocator failure rollback.
for target in x86_64 riscv aarch64 loongarch64; do
    cp "$work/$target.s" "$work/$target.invalid.s"
    printf '\n__minias_invalid_instruction__\n' >>"$work/$target.invalid.s"
    if "$MINIAS" --target="$target" -o "$work/$target.invalid.o" \
        "$work/$target.invalid.s" >"$work/$target.invalid.log" 2>&1; then
        echo "invalid Target input incorrectly accepted: $target" >&2
        exit 1
    fi
    if test -e "$work/$target.invalid.o"; then
        echo "partial object written on failed Target input: $target" >&2
        exit 1
    fi
done

echo 'MINIAS_MC_ENCODE_RESULT=PASS targets=4 instruction_fixups=4 explicit_section=1 cfi_synthetic=1 failed_outputs=0'
