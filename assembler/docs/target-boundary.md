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
   target supplies parse_mcinst. The RISC-V production encoder now uses
   typed MCInst operands for the R/I/U arithmetic families, byte/half/word/
   dword memory operands, B-type local and long branches and symbolic JAL.
   ISA M and RV64 word arithmetic share the same tables; GNU compatibility
   aliases (such as add/sub immediate forms) remain supported.
   The obsolete fixed-width integer/branch/JAL decoding branches were
   removed (hundreds of redundant lines); legacy symbolic memory expansion
   remains until matching relocation-aware fragments replace it. MCInst
   operands allocate symbol-expression storage only for symbolic operands:
   register/immediate-heavy input no longer pays the former 256-byte symbol
   buffer per operand. The second grouped migration now also covers FP F/D
   operations, conversions and memory operands, vector unit-stride/config,
   atomics (AMO, LR/SC) and CSR aliases, with FPR/vector register operand
   classes. Their duplicate legacy byte emitters have been deleted.
   The third batch migrates compressed c.li, pseudo-register operations
   (ret/nop/mv/jr/snez/seqz/neg/negw/not), jalr addressing variants, frcsr/fscsr,
   sfence.vma, fence/fence.i, supervisor/system instructions, pause and wfi.
   Their previous text-driven emitters are deleted. Matching GNU GAS byte
   comparisons cover both fixed 32-bit encodings and compressed c.li.
   Almost all redundant legacy fixed-opcode measurement branches are removed;
   target measurement defers fixed-size operand validation to MCInst on both
   strict-I and default profiles. Strict RV32I/RV64I *still* executes its
   exhaustive post-encoding machine-word check. Symbolic GPR load/store
   are now *typed two-instruction* MCInst fragments, carrying the symbolic
   expression and both data and address registers. Their target emitter
   creates AUIPC plus signed load/store and PCREL_HI20/PCREL_LO12_I/S typed
   fixups without reparsing operand text. The old ~6 KB duplicate GPR
   memory emitter is deleted. Ordinary signed memory offsets now get
   12-bit range checking at the target operand boundary.
   Variable-size `li`, `call`, `tail`, `la` and `lla` now parse
   into persistent, typed MCInst (registers, exact unsigned 64-bit constant,
   and symbolic addends), and only their target-owned layout-size oracle
   remains; the entire duplicate text-parsing/emission tail is removed.
   The typed encoder retains PIC/NOPIC GOT versus PCREL decisions and
   emits CALL_PLT, GOT_HI20, PCREL_HI20 and LO12_I fixups. RV32 LI width
   is determined by the actual target XLEN on every profile, including
   RV32IM—not merely by enabling the strict-I validator. Even the raw
   R/I `.insn` escape now parses exactly once into a target-owned typed
   MCInst machine word. The obsolete conditional fallback decoder is
   removed entirely: **all remaining RISC-V instruction emission consumes
   MCInst**. Multiword LI/CALL/TAIL/LA and symbolic loads/stores still use
   target-owned legacy *measurement* to determine their total fragment size;
   replacing those duplicate size parsers with deferred fragment sizing is
   the next architectural task. RV32 shift widths are validated at the
   typed parser.
   Vector register classes are distinguished from integer/FP registers.
   Comment stripping is target-configurable and .option
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
