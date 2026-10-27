# MiniAS target boundary: implementation and remaining gates

## First structural migration (not final architecture completion)

Common assembler owns sections, symbols, subsection layout, source traversal,
macros and ELF object creation. The target now owns instruction measurement,
encoding, branch classification, executable alignment bytes, relocation
number mapping, and strict post-encoding ISA verification via MiniAsTargetOps.

Command line options are resolved as a **combined** -march/-mabi profile.
XLEN and ELFCLASS cannot conflict; hard-float ABI requires the matching ISA
extensions; RVE ABI requires an E base architecture when -march is present.
Explicit -march=rv32i/rv64i retains existing fail-closed machine-code checks.

## Still required before claiming multi-target architecture complete

1. Implement typed MCInst and target-specific source lexing/directive parsing;
   comment stripping is target-configurable and .option dispatch is owned by the
   RISC-V target; the common parser still owns GNU-style statement grammar.
2. Replace statement-size reparsing with stable MCFragments and layout
   relaxation/fixup callbacks; statement ordinal fixes macro identity today.
3. Unify expression grammars and properly separate Fixup from ELF Relocation.
4. Extend target-owned ELF attributes to truthful non-I profiles, including
   ISA changes inside .option regions (currently strict I only).
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
