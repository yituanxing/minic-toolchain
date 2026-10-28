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
for backend in riscv_encode.c x86_64_target.c loongarch64_target.c; do
    if test -f "assembler/src/$backend" && \
       grep -q 'minias_section_append(as,' "assembler/src/$backend"; then
        echo "Direct Section append in target encoder: $backend" >&2
        exit 1
    fi
done
# AArch64 executable alignment is a separate Target callback. It may append
# NOP fill through Common Section ownership; instruction encoder may not.
grep -q 'return minias_emit_instruction_bytes(as, section, bytes, sizeof(bytes));' \
    assembler/src/aarch64_target.c
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
echo 'MINIAS_MC_TARGET_BOUNDARY=PASS ISA_fixups=private target_bytes=fragment_owned site=immutable shared_lexer=4 DWARF_sort=reentrant'
