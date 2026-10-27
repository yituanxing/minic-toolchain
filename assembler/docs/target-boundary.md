# MiniAS target boundary: implementation and remaining gates

## First structural migration (not final architecture completion)

Common assembler owns sections, symbols, subsection layout, source traversal,
macros and ELF object creation. The target now owns instruction measurement,
encoding, branch relaxation, executable alignment bytes, fixup-to-ELF
relocation mapping, and strict post-encoding ISA verification via
MiniAsTargetOps.

Command line options are resolved as a **combined** -march/-mabi profile.
XLEN and ELFCLASS cannot conflict; hard-float ABI requires the matching ISA
extensions; RVE ABI requires an E base architecture when -march is present.
Explicit -march=rv32i/rv64i retains existing fail-closed machine-code checks.

## Still required before claiming multi-target architecture complete

1. AArch64 now parses register/immediate/symbol operands into a typed
   MCInst exactly once at fragment creation; encode consumes MCInst instead
   of re-splitting source text. MCInst owns a flexible operand array, without
   a hard-coded two-operand limit. Common core owns MCInst lifecycle, and the
   target supplies parse_mcinst. The larger RISC-V encoder still uses legacy
   operand strings and must be migrated in grouped opcode families before
   retiring that path. Comment stripping is target-configurable and .option
   dispatch belongs to the RISC-V target; common parser still owns GNU-style
   statement grammar. Raw executable data/.org ISA guards are now target-owned,
   allowing the AArch64 backend to assemble raw executable .word where legal.
2. The common core now has persistent parsed instruction/data/alignment
   fragments with stable statement identities, target-driven size growth,
   and in-place branch relaxation for position-independent source. Label,
   .size and .p2align anchors are updated without reopening the input.
   Position-dependent .set/.equ/.org/.if conservatively use the historical
   reparsing fallback. The remaining work is a general typed deferred
   expression/fixup system that removes this fallback altogether.
3. A typed semantic MiniAsFixup now represents machine and data field
   adjustments (ABS/ADD/SUB, RV PCREL/GOT/CALL/JAL, A64 CALL/JUMP). Each
   backend translates fixup kinds to ELF relocation numbers **only at ELF
   serialization**, after field-boundary validation. The old common
   MiniAsReloc array and eager R_* encoding are removed. Remaining:
   symbol-aware deferred expression evaluation, field bit-layout callbacks,
   and linker/assembler relaxation at arbitrary instruction sites.
4. Explicit -march now checks M/A/F/D/C/V/Zicsr/Zifencei/Zbb opcode
   families and emits canonical Tag_RISCV_arch. Finish machine-word-wide
   validation, full ISA version/dependency grammar and target option-region
   semantics. For selected -march, unsupported .option arch/rvc/relax
   must fail rather than silently changing the declared ELF architecture.
5. Complete .option arch/rvc/relax semantics instead of ignored legacy modes.
6. Complete DWARF CFI .eh_frame generation; boundary tags currently do not
   emit unwind information.
7. Expand the new AArch64 vertical-slice backend into a usable GNU-compatible
   target, with full target ISA/ABI parsing, registers and relocation coverage.
8. Verify exact-head A0, RV32I/RV64I, Linux corpus, object oracle and P1 boot
   before merging. Prior green CI is not proof for a new head.

The feature parser recognizes common basic single-letter ISA spellings and
named _z*/_s*/_x* suffixes but does not fully validate ISA versions, extension
dependencies or instruction-by-instruction features for non-I profiles.

## Exact-head acceptance caveat (2026-10-27)

The opt-in 3536 Linux build gate on `d0d66136` showed successful native35,
C149 and several frozen C windows, but other windows failed during **MiniC
C-to-assembly generation**, reporting `indirect call argument type does not
match declaration` in unrelated translation units. This cannot be called
a MiniAS success or failure without separating compiler generation from
MiniAS assembly and frozen semantic oracles. Do not waive failed windows,
and do not rerun expensive full corpus for each MiniAS-only fix. A separate
MiniC owner should resolve the upstream C-to-S blocker before the next
consolidated exact-head acceptance.
