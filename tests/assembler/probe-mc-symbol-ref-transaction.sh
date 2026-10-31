#!/usr/bin/env bash
# Symbol binding must not leak speculative parser state into the ELF object.
# Compare host x86 GNU semantics and exercise generic SymbolID/REL(A) data
# paths on all four MiniAS targets without cross compilers or Linux inputs.
set -Eeuo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$project_root/build/release/bin/minic-as}"
test -x "$MINIAS"
for tool in as readelf python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/references.s" <<'ASM'
.data
.globl referenced_table
.weak weak_undefined
referenced_table:
.set lexical_counter,2
    .long lexical_counter
.set lexical_counter,3
    .long lexical_counter
    .long external_reference + 4
    .long external_reference + 4
    .quad weak_undefined + 9
    .quad .Lforward + 2
.if 0
    .quad skipped_symbol_must_not_exist
.endif
.macro emit_external addend
    .long external_reference + \addend
.endm
.rept 2
    emit_external 4
.endr
.irp addend,7,9
    .quad weak_undefined + \addend
.endr
.Lforward:
    .byte 0xa1
ASM

as --64 -o "$work/gnu.o" "$work/references.s"
for target in x86_64 riscv aarch64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target.o" "$work/references.s"
done

python3 - "$work/gnu.o" "$work/x86_64.o" "$work/riscv.o" \
    "$work/aarch64.o" "$work/loongarch64.o" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

def readelf(path, *args):
    return subprocess.check_output(['readelf', *args, path], text=True)

def section_data(path):
    section = readelf(path, '-SW')
    found = re.search(
        r'^\s*\[\s*\d+\]\s+\.data\s+PROGBITS\s+'
        r'[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\b',
        section, re.M)
    if not found:
        raise AssertionError(f'no .data section in {path}')
    start, size = (int(x, 16) for x in found.groups())
    contents = Path(path).read_bytes()
    if start + size > len(contents):
        raise AssertionError(f'out-of-bounds ELF .data for {path}')
    return contents[start:start + size]

def relocations(path):
    # Ignore ELF symbol table indices: they are writer-private.  Preserve
    # actual section-relative offset, R_* kind, symbol and addend.
    result = []
    section = None
    for line in readelf(path, '-rW').splitlines():
        header = re.search(r"Relocation section '([^']+)'", line)
        if header:
            section = header.group(1)
        field = re.match(
            r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+'
            r'(R_\S+)\s+[0-9a-fA-F]+\s+(\S+)'
            r'(?:\s+\+\s+(\S+))?\s*$', line)
        if field:
            off, kind, name, addend = field.groups()
            result.append((section, int(off, 16), kind, name,
                           int(addend, 16) if addend is not None else 0))
    return sorted(result)

oracle = section_data(sys.argv[1])
expected_start = bytes.fromhex('02 00 00 00 03 00 00 00')
if len(oracle) != 57 or not oracle.startswith(expected_start) or oracle[-1] != 0xa1:
    raise AssertionError(f'GNU lexical/source-frame oracle mismatch: {oracle.hex()}')

gnu_rel = relocations(sys.argv[1])
if len(gnu_rel) != 8:
    raise AssertionError(f'expected 8 GNU relocations, got {gnu_rel}')

for target, path in zip(('x86_64', 'riscv', 'aarch64', 'loongarch64'), sys.argv[2:]):
    data = section_data(path)
    if data != oracle:
        raise AssertionError(f'{target} data differs from GNU: {data.hex()} != {oracle.hex()}')
    syms = readelf(path, '-sW')
    if 'skipped_symbol_must_not_exist' in syms:
        raise AssertionError(f'{target}: symbol from skipped SourceFrame branch leaked')
    for name, binding in (('external_reference', 'GLOBAL'),
                          ('weak_undefined', 'WEAK')):
        if not re.search(rf'\b{binding}\s+DEFAULT\s+UND\s+{name}\s*$', syms, re.M):
            raise AssertionError(f'{target}: missing {binding} undefined {name}')
    rel = relocations(path)
    if len(rel) != 8:
        raise AssertionError(f'{target}: 8 expected relocations, got {rel}')
    for name, count in (('external_reference', 4), ('weak_undefined', 3)):
        if sum(item[3] == name for item in rel) != count:
            raise AssertionError(f'{target}: wrong relocation count for {name}: {rel}')
    if target == 'x86_64':
        if rel != gnu_rel:
            raise AssertionError(f'x86 GNU relocation parity mismatch: {rel} != {gnu_rel}')
        # x86 opts into section-symbol folding for defined local ABS
        # relocations.  The original disposable .L label must not survive
        # merely to give the relocation an ELF symbol-table ordinal.
        if '.Lforward' in syms or sum(item[3] == '.data' for item in rel) != 1:
            raise AssertionError(f'x86 local section-symbol folding broken: {rel}')

print('MINIAS_MC_SYMBOL_REF_DATA=PASS targets=4 GNU_bytes=exact x86_GNU_relocs=exact section_fold=1 repeated_fixups=7 no_skipped_symbol=1')
PY

# The RISC-V PC-relative pair uses a *private, requested* anchor: the
# LO12 relocation must retain its matching symbol through ELF liveness.
cat >"$work/rv-pcrel.s" <<'ASM'
.text
.globl pcrel_anchor_test
pcrel_anchor_test:
    la a0, external_pcrel_target
ASM
"$MINIAS" --target=riscv -o "$work/rv-pcrel.o" "$work/rv-pcrel.s"
readelf -rW "$work/rv-pcrel.o" >"$work/rv-pcrel.reloc"
readelf -sW "$work/rv-pcrel.o" >"$work/rv-pcrel.syms"
grep -q 'R_RISCV_PCREL_HI20' "$work/rv-pcrel.reloc"
grep -q 'R_RISCV_PCREL_LO12_I' "$work/rv-pcrel.reloc"
grep -q 'external_pcrel_target' "$work/rv-pcrel.reloc"
grep -q '\.Lminias_private_' "$work/rv-pcrel.syms"

# RV symbolic MCInst paths use the same two-phase reference contract as
# generic DATA, but their encoding policies are target-specific: CALL_PLT,
# PIC GOT, PCREL, memory HI/LO, numeric labels and cross-subsection JAL.
cat >"$work/rv-symbolic.s" <<'ASM'
.option norvc
.option norelax
.text
.globl rv_symbol_ref_txn
rv_symbol_ref_txn:
    call rv_external_call@plt
    tail rv_external_tail
    lla a0, rv_external_data + 8
.option pic
    la a1, rv_external_data
.option nopic
    la a2, rv_external_data + 4
    lw a3, rv_external_load
    sw a3, rv_external_store + 4, t1
1:
    beq a0, a1, 1f
    addi a0, a0, 1
1:
    jal zero, rv_other_subsection
.subsection 1
rv_other_subsection:
    ret
ASM
"$MINIAS" --target=riscv -o "$work/rv-symbolic.o" "$work/rv-symbolic.s"
readelf -rW "$work/rv-symbolic.o" >"$work/rv-symbolic.reloc"
readelf -sW "$work/rv-symbolic.o" >"$work/rv-symbolic.syms"
test "$(grep -c 'R_RISCV_CALL_PLT' "$work/rv-symbolic.reloc")" -eq 2
test "$(grep -c 'R_RISCV_GOT_HI20' "$work/rv-symbolic.reloc")" -eq 1
test "$(grep -c 'R_RISCV_PCREL_HI20' "$work/rv-symbolic.reloc")" -eq 4
test "$(grep -c 'R_RISCV_PCREL_LO12_I' "$work/rv-symbolic.reloc")" -eq 4
test "$(grep -c 'R_RISCV_PCREL_LO12_S' "$work/rv-symbolic.reloc")" -eq 1
grep -q 'R_RISCV_JAL' "$work/rv-symbolic.reloc"
if grep -q 'rv_external_call@plt' "$work/rv-symbolic.syms"; then
    echo 'RV @plt parser suffix leaked to ELF symbol table' >&2
    exit 1
fi

# A short conditional branch must grow after layout and still bind the
# forward label through its original retained SymbolID, not a saved pointer.
cat >"$work/rv-long-branch.s" <<'ASM'
.option norvc
.option norelax
.text
.globl rv_forward_growth
rv_forward_growth:
    beq a0, a1, 1f
    .rept 1150
        nop
    .endr
1:
    ret
ASM
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=riscv \
    -o "$work/rv-long-branch.o" "$work/rv-long-branch.s" \
    2>"$work/rv-long-branch.log"
grep -q 'minias-fragment-relayout:inplace-passes=' "$work/rv-long-branch.log"

# A lexical failure after a valid symbol-bearing fragment must never write
# a partial object.  This black-box case checks output atomicity; actual
# symbol_count rollback requires a separate white-box in-process assertion.
cat >"$work/invalid.s" <<'ASM'
.data
    .long valid_but_unresolved + 4
    .long speculative_name + (1 + )
ASM
for target in x86_64 riscv aarch64 loongarch64; do
    if "$MINIAS" --target="$target" -o "$work/$target.invalid.o" \
        "$work/invalid.s" >"$work/$target.invalid.log" 2>&1; then
        echo "invalid symbolic expression accepted: $target" >&2
        exit 1
    fi
    if test -e "$work/$target.invalid.o"; then
        echo "invalid symbolic expression emitted a partial object: $target" >&2
        exit 1
    fi
done

echo 'MINIAS_MC_SYMBOL_REF_TRANSACTION=PASS targets=4 private_pcrel=1 rv_target_refs=8 rv_long_branch=1 invalid_no_object=4'
