#!/usr/bin/env bash
# Guard the actual boundary: ISA-private fixup kinds and machine-code emit
# callbacks cannot leak into MC Core, ELF serialization, or the CLI.
set -Eeuo pipefail
if grep -Eq 'MINIAS_FIXUP_(RV_|A64_|X64_|LARCH_)' \
    assembler/src/minias_internal.h assembler/src/assembler.c \
    assembler/src/elf_writer.c; then
    echo 'Target FixupKind leaked into MC Core' >&2
    exit 1
fi
for backend in riscv_target.c riscv_encode.c aarch64_target.c \
               x86_64_target.c loongarch64_target.c; do
    if test -f "assembler/src/$backend" && \
       grep -q 'minias_section_append(as,' "assembler/src/$backend"; then
        echo "Direct Section append in target encoder: $backend" >&2
        exit 1
    fi
done
# A byte-only capture is not an MC transaction.  Every Target now returns
# fragment-owned Fixup/Symbol requests to the Common encoder transaction;
# legacy direct edits of the global object graph are forbidden.  Inspect
# tokens rather than raw grep hits so comments cannot manufacture a PASS.
python3 - <<'PY'
from pathlib import Path
import re

targets = ('riscv_target.c', 'riscv_encode.c', 'aarch64_target.c',
           'x86_64_target.c', 'loongarch64_target.c')
bad = []
patterns = {
    'direct Fixup insertion': r'\bminias_add_fixup\s*\(',
    'global Fixup array access': r'\bas\s*->\s*fixups\s*\[',
    'global ELF flag mutation':
        r'\bas\s*->\s*elf_flags\s*(?:\|=|&=|\^=|\+=|-=|=(?!=)|\+\+|--)',
    'global synthetic-anchor counter mutation':
        r'\bas\s*->\s*pcrel_anchor_counter\s*(?:\+\+|--|\+=|-=|=(?!=))',
    'direct synthetic Symbol insertion':
        r'\bminias_get_symbol\s*\(\s*as\s*,[^;]*?,\s*true\s*\)',
    'Target bypasses Common symbol-reference lookup':
        r'\bminias_get_symbol\s*\(',
    'legacy name-based target Fixup request':
        r'\bminias_encode_add_fixup\s*\(',
    'legacy symbolic MCOperand representation':
        r'\bMINIAS_MC_SYMBOL(?:_DIFFERENCE)?\b',
}
for name in targets:
    path = Path('assembler/src') / name
    source = path.read_text(encoding='utf-8')
    # Drop C comments and literals without changing line breaks, then check
    # whole tokens across whitespace/newlines.  The functional test below is
    # a separate gate: source spelling alone is not semantic proof.
    cleaned = re.sub(
        r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda m: '\n' * m.group().count('\n') + ' ', source)
    for label, pattern in patterns.items():
        match = re.search(pattern, cleaned, flags=re.DOTALL)
        if match:
            line = cleaned.count('\n', 0, match.start()) + 1
            bad.append(f'{path}:{line}: {label}')
if bad:
    raise SystemExit('MiniAS Target/Common encode ownership violation:\n' +
                     '\n'.join(bad))

core = Path('assembler/src/minias_internal.h').read_text(encoding='utf-8')
for api in ('minias_encode_add_elf_flags', 'minias_encode_add_fixup_ref',
            'minias_encode_request_anchor_ref', 'minias_bind_symbol_ref',
            'minias_symbol_ref_lookup'):
    if not re.search(r'\b' + api + r'\s*\(', core):
        raise SystemExit('Missing Common transaction API: ' + api)
for legacy in ('minias_encode_add_fixup', 'minias_encode_request_anchor',
               'has_symbol_ref'):
    if re.search(r'\b' + legacy + r'\b', core):
        raise SystemExit('Legacy dual-track transaction API remains: ' + legacy)
print('MINIAS_MC_ENCODE_BOUNDARY=PASS target_mutation=none requests=typed')
PY
# Code alignment is a Target-owned NOP policy but still emits through the
# identical MC byte transaction as every instruction. No architecture gets
# a special direct-Section bridge for ALIGN or for an instruction emitter.
grep -q 'return minias_emit_instruction_bytes(as, section, bytes, sizeof(bytes));' \
    assembler/src/aarch64_target.c
grep -q 'minias_emit_instruction_bytes' assembler/src/riscv_target.c
grep -q 'minias_emit_instruction_bytes' assembler/src/x86_64_target.c
grep -q 'minias_emit_instruction_bytes' assembler/src/loongarch64_target.c
# Each Target must reserve a separate kind namespace.
for header in riscv_fixup_kinds.h aarch64_fixup_kinds.h \
              x86_64_fixup_kinds.h loongarch64_fixup_kinds.h; do
    test -s "assembler/src/$header"
done
grep -q 'minias_emit_instruction_bytes' assembler/src/assembler.c
grep -q 'MiniAsTargetSiteState' assembler/src/minias_internal.h
# LLVM MC-style lexical ownership: balanced operand grouping lives once in
# Common MC. All four Target parsers consume it, and retain ISA legality.
test -s assembler/src/mc_lexer.c
for target in riscv_encode.c aarch64_target.c x86_64_target.c \
              loongarch64_target.c; do
    if ! grep -q 'minias_split_mc_operands' "assembler/src/$target"; then
        echo "Target bypasses shared MC operand lexer: $target" >&2
        exit 1
    fi
done
if grep -q 'comparator_as' assembler/src/debug_line.c; then
    echo 'DWARF line comparator has global assembly context' >&2
    exit 1
fi
if grep -Eq '(strict_i_xlen|strict_rv64i|isa_explicit|isa_features|target_attributes|target_option_stack|target_option_depth)' assembler/src/minias_internal.h; then
    echo 'RISC-V profile or option stack leaked into MC Core' >&2
    exit 1
fi
# AT&T base/index/SIB/FS/GS is not a cross-ISA MC addressing contract.
# Memory stays a semantic operand kind, while its data layout and nested
# allocations are private to the owning Target.
if grep -Eq 'MiniAsMCMemory|has_index|pc_relative|address_space' \
    assembler/src/minias_internal.h; then
    echo 'x86 memory addressing representation leaked into MC Core' >&2
    exit 1
fi
if grep -Eq 'MiniAsMCMemory|->memory->' \
    assembler/src/minias_internal.h assembler/src/mc_operand_semantics.c; then
    echo 'Common destructor depends on an ISA memory payload' >&2
    exit 1
fi
grep -q 'void (\*dispose_memory)(void \*);' assembler/src/minias_internal.h
grep -q 'typedef struct X64Memory' assembler/src/x86_64_target.c
grep -q 'm->dispose_memory=x64_dispose_memory;' assembler/src/x86_64_target.c
echo 'MINIAS_MC_TARGET_BOUNDARY=PASS ISA_fixups=private target_bytes=fragment_owned site=immutable shared_lexer=4 DWARF_sort=reentrant memory_payload=target_owned'
