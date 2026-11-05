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
   MCInst**. This batch also eliminates the **last duplicate RISC-V
   textual operand measurement grammar**. The core now installs the parsed
   instruction first, then queries a target-owned `measure_mcinst` hook
   before placing its fragment. 2B compressed, 4B regular, 8B symbolic
   memory/CALL/LA and variable-size 32/64-bit LI measurements derive
   directly from the stored MCInst. The old RISC-V text-based measure
   function and duplicate opcode classifier are deleted. The AArch64
   vertical slice now uses the same **parse-then-measure MCInst hook**,
   removing its duplicate mnemonic allowlist. It measures fixed 4B
   instructions from parsed MCInst, checked against GNU AS and ELF .size.
   Both targets now share the mandatory parse-first/measure-from-MCInst
   contract. The obsolete optional mnemonic-based `measure` dispatch and
   its core fallback have been deleted. ISA/ABI feature checks use a
   separate optional `validate_instruction` hook before the single parse;
   branch relaxation still follows fragment measurement.
   RV32 shift widths are validated at the typed parser.
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

## Multi-target profile ownership checkpoint (2026-10-27)

The previous CLI selected both backends **and** encoded RISC-V-only profile
resolution and AArch64-only option validation in `tools/minic-as/main.c`.
That leaked ISA/ABI policy into the driver and forced driver modification
whenever a new backend was added.

The driver now resolves a backend through one target registry, asks that
backend's `resolve_profile` hook for a validated `MiniAsAssemblyConfig`,
checks that the profile returns the requested target, and calls the existing
common `minias_assemble_config` entry. RISC-V still owns its full
`-march/-mabi` validation, ELF32 selection, ISA bitset and Tag_RISCV_arch;
AArch64 still rejects all unsupported `-march/-mabi` combinations. The
default target and the historical public assembler API are unchanged.

Architecture review after this batch: adding a backend requires registering
its target constructor and adding its implementation to the build, **not**
adding ISA/ABI branches to the CLI or modifying common instruction algorithms.
This is a boundary improvement, not proof of x86-64 or LoongArch64 support.
The target-specific fields still resident in `MiniAs`, the shared
`MiniAsFixupKind` enum, full deferred expressions, full relax/fixup bitfield
rules and CFI remain separate architecture work. Follow-up multi-target
corpus discovery must keep failure cohorts target-specific and only elevate
reproducibly common mechanisms to the shared architecture.

## AArch64 integer-target expansion (2026-10-27)

The first grouped AArch64 compiler-output barrier is implemented inside the
existing Target-owned typed MCInst parser and encoder, **not** through
source-text re-parsing at emit time: MOV register/MOVZ immediate, ADD/SUB
register or immediate, CMP, integer AND/ORR/EOR, scaled unsigned-offset
LDR/STR byte/halfword/word/doubleword, and signed STP/LDP pair memory
pre-/post-/offset. GNU AArch64 assembler byte equality is required on
the grouped instruction matrix. Invalid register widths, unmapped addressing
modes, signed-scale violations and unsupported options fail closed.

No Common instruction family, Section, Symbol, Fragment or ELF code was changed.
This target still intentionally lacks ADRP/GOT, conditional branches,
shifts, extend/bitfield and full CFI. New ISA implementations must add
AArch64 MCInst cases rather than duplicate the common layout/ELF pipeline.

## GCC RV64I native C→S evidence and attributes (2026-10-27)

The true GNU GCC `-S -march=rv64i -mabi=lp64` input includes
`.attribute arch`, `.attribute stack_align` and
`.attribute unaligned_access`, in addition to integer opcodes.
The prior assembler rejected `.attribute` before it ever reached the
strict-I instruction encoder. These directives now have a target-owned
strict parser: the declared ISA must equal the normalized selected
`-march`; numeric tag 4/6 values are emitted into the target ELF
`.riscv.attributes` subsection (length fields updated), duplicates
with conflicting values fail closed, and unknown tags reject.

This is not permission for the compiler or `asm` to generate M/CSR opcodes
under RV64I: handwritten extension instructions still go through the
same strict target machine-ISA gate. Common ELF owns serialized sections,
while RISC-V owns contents of its architecture attributes.

## First x86-64 and LoongArch64 Target implementations

Two additional targets are registered without changing Common parsing,
Expression, MCInst/Fragment layout, sections, symbol binding or ELF serializer.
They both use the same lifecycle hooks previously exercised by RISC-V and
AArch64. These first targets are **strict integer vertical slices**, not
claims of Linux/GNU full ISA support. x86-64 currently supports selected
GNU AT&T integer register-to-register operations, register stack operations,
a few 0-operand system instructions and 32-bit register mov immediates.
LoongArch64 uses the GNU binutils LA64 opcode table for integer
register-register (ADD/SUB/AND/OR/XOR/SLT) and signed-12 ADDI/LD/ST,
NOP, RET, MOV aliases. The target owns its 4-byte NOP alignment.

ELF machines are EM_X86_64=62 and EM_LOONGARCH=258. The LoongArch
psABI encodes LP64S/LP64F/LP64D in low three e_flags bits (1/2/3)
plus ABI-v1 version bits (0x40), not generic ELF assumptions.
Unknown ISA, ABI, encodings or relocations fail closed. No general x86
ModR/M memory operand grammar or LoongArch relocations have been
invented in Common; those are explicit remaining per-target obligations.

## Multi-target optional callbacks (after real x86-64 crash, 2026-10-27)

The first actual x86-64 ELF smoke crashed at the **first directive** because
Common unconditionally dereferenced `parse_target_directive`, even though
the Target table deliberately left that hook absent. The ELF writer would
likewise dereference absent `object_attributes`. Both hooks are genuinely
optional by architecture contract: Common now checks these pointers, and
the ELF writer initializes its object-attributes result to none before a
conditional call. The mandatory instruction parse/measure/encode and required
relocation, validation, code-alignment hooks remain mandatory.

This is a **single shared API correction** rather than copied per-target
dummy methods or an architecture-specific `if (x86)` workaround. The
regression gate checks GNU machine-code/ELF parity for BOTH new targets.

## Real complete Linux first-blocker Pareto: target-only ISA batch

x86 Linux GNU `.s` now contains 2,763 complete Kbuild inputs.
Real frozen shard evidence showed **314/394** files in one shard blocked
at Intel CET `endbr64`. The x86 Target now recognizes its 4-byte
encoding and canonical sign-extended group-1 ADD/SUB immediate (imm8/imm32)
on 32/64-bit GPRs. LoongArch64 Linux samples are blocked at the register
`jr` pseudo in roughly dozens of objects per shard, now mapped to
`jirl $zero,$reg,0`. Unknown LoongArch relocations (e.g. PCALA)
and AArch64 CFI remain explicitly unsupported.

The GNU assembler differential matrix includes CET endbr64, immediate
ALU forms and LoongArch jr. Neither common instruction dispatch, ELF
serialization, nor architecture-specific duplicate copies were modified.
The Kbuild per-child scanner now enforces 2GiB virtual memory and emits
per-25-file progress records, so VM shutdowns can be distinguished from
actual MiniAS instruction problems before repeating any full 3x7 campaign.

## Shared typed memory operand and x86 GNU addressing oracle

The frozen Linux SHA-bound error distribution after the first four targets
showed x86 assembly blocked at **MOVQ (1289 owners) and MOVL (265 owners)**,
principally due missing memory operand syntax. Instead of extracting x86
ModRM knowledge into Common or reparsing raw text at encoding, the common
typed `MiniAsMCOperand` gains an optional pointer to a generic
base/index/scale/displacement `MiniAsMCMemory` (via a C11 union with
the existing symbol pointer, avoiding a per-integer memory increase).
Common owns destruction of typed memory and its optional symbol.

The x86 target implements a validated GNU AT&T numeric disp(%base)
subset of MOVL/MOVQ, with REX, ModRM, mandatory RSP/R12 SIB,
RBP/R13 zero displacement, disp8/disp32 selection and strict
register-width/overflow validation. The GNU-as byte oracle covers
positive and negative addressing cases including high registers.
Symbolic relocation and index/scale remain explicitly unsupported; no
silently ignored expression is accepted. The LoongArch and AArch64
backends need no modifications to reuse the generic operand schema.

## Batch: GNU Linux error Pareto to grouped encoders (2026-10-27)

The x86 backend's existing **one typed register–memory instruction** now
covers CMP/TEST/ADD/SUB/AND/OR/XOR as well as MOV for numeric
`disp(%base)` with the target-specific ModRM/SIB/REX encoder. The shared
MCInst and ELF paths do not change; direction-specific AT&T opcode pairs
are declared once in the x86 target. Register-register TEST uses opcode 0x85.

The LoongArch64 target extends its existing memory opcode table with
byte/halfword signed/unsigned loads and stores, and a distinct sign-scaled
14-bit byte-offset family for `LDPTR/STPTR.W/D` (si14<<2). Signed range,
alignment, and machine encoding are checked at the target parser boundary;
symbolic PC-relative and PLT relocations remain unsupported. GNU AS/objcopy
byte-for-byte oracle now verifies positive and negative boundary cases
for both architectures on the same S source.

This batch intentionally avoids fake `pcalau12i` relocs, skipping CFI, and
independently cloned ELF algorithms. The existing 2x7 **frozen** GNU Linux
corpora are the next affected-only error census; no new Linux C compilation.

## x86 scaled index as typed target addressing

The same shared `MiniAsMCMemory` (base/index/scale/displacement) now drives
GNU AT&T `disp(%base,%index,scale)`, including 1/2/4/8 index scales,
REX.X/B extensions, mandatory SIB for RSP/R12 base, and RBP/R13
displacement edge cases. Top-level operand splitting is target-specific
because x86 addressing parentheses contain commas. Symbol relocations,
index-without-base and other incomplete forms fail closed.

The exact GNU parity matrix covers indexed register loads/stores and
CMP/TEST/ALU, including high register/scale boundaries and invalid index,
scale and overflow cases. Trigger the 2x7 affected-ISA Linux census only
when a grouped target batch adds `[minias-replay-2x7]` to its commit
message, or on manual dispatch. No recompiled GCC objects.

## LoongArch64 GNU-defined integer opcode families

Genuine original LoongArch Linux first blockers now show `pcalau12i`
830 owners, `andi` 100, `slli.w` 86, `lu12i.w` 131, plus
`masknez` and other base integer forms. Target-owned instruction tables
now describe signed-12, unsigned-12, signed-20 upper immediates,
5-/6-bit shift amounts, common integer register shifts and multiplication.
Range validation is specific to each immediate field, not shared generic
integer overflow logic. `pcalau12i` numeric immediates are encoded as
the GNU 20-bit form, but **symbolic %pc_hi20 relocations remain unsupported
and MUST be added as a target-specific fixup** before real Linux acceptance.

The GNU LoongArch assembler byte matrix includes signed minima/maxima,
all new immediate families and invalid shift/unsigned/range cases.
ELF, Common IR, CFI and other target encoders remain unchanged.

## Real Linux critical-path relocation batch (2026-10-27)

The previous pattern (implement one numeric instruction, rerun seven complete
GNU-Linux shards, gain only 2 files) was structurally inefficient. The next
batch addresses three high-volume shared ISA requirements in one target:

- `%pc_hi20(sym+addend)` -> R_LARCH_PCALA_HI20 71 and
  `%pc_lo12(sym+addend)` -> R_LARCH_PCALA_LO12 72.
- B/BL (R_LARCH_B26 66); BEQZ/BNEZ (R_LARCH_B21 65);
  BEQ/BNE/BLT/BGE/BLTU/BGEU (R_LARCH_B16 64), with aligned
  same-section branch resolution and out-of-range checks.
- Common SymbolExpr, fixup allocator, ELF RELA writer reused verbatim:
  the LoongArch Target only declares accepted encodings and numeric
  ELF relocation identity; its decode stores typed MCInst operands.

The GNU oracle now compares target .text bytes plus explicit ELF relocation
families, and rejects unimplemented GOT/TLS modifiers. It is not yet full
Linux machine/relocation parity: GNU may emit additional relaxation
relocations and not all LoongArch forms are parsed. The next campaign should
first pass this exact-source GNU unit oracle, then rerun only the existing
seven LoongArch frozen shards, with no kernel/producer rebuild.

## RV architecture cross-audit and corrective convergence (2026-10-27)

The **historically verified RISC-V** backend separates instruction meaning
(`RV_MC_*`) from semantic fixup identity
(`MINIAS_FIXUP_RV_*`) and uses the shared SymbolExpr, common
`minias_add_fixup`, section-offset layout, target relocation lookup,
and common ELF RELA serialization. The first LoongArch symbolic batch
reused the latter pipeline but incorrectly reused FixupKind enum values as
MC opcode numbers. That concealed a *real architecture violation* and
could introduce opcode collisions when adding more families.

This audit repairs LoongArch to use its own Target-only `LA_MC_*`
opcode family and converts to separate `MINIAS_FIXUP_LARCH_*` exactly
at encode/fixup placement. No duplicated symbol table/ELF fixup engine
and no RV code modifications. LoongArch branch register bit layout was
also corrected from the GNU word oracle
(`beqz $a0` = `0x40000080`; `beq $a0,$a1` = `0x58000085`).

RV ISA/ABI configuration, PIC/GOT semantics, .option architecture state,
RVC relaxation, A0 corpus and kernel P1 are **not equivalent** to the
new targets yet: those stronger RV capabilities remain target-specific
requirements, and GNU AArch64/x86/LoongArch Linux acceptance is not
asserted. New architectures must continue adding capabilities to a
stable shared pipeline instead of copying RV's implementation blindly.

## LoongArch branch / GNU oracle + 7 frozen shard verification (2026-10-27)

In addition to external-symbol B16/B21/B26 RELA agreement, exercise
same-section **forward/backward local label** displacement against GNU
machine bytes. The LoongArch parser reuses the mature RV pattern:
SymbolExpr at parse -> MCInst target opcode -> layout-resolved same-section
immediate or common Fixup -> Target relocation number -> common ELF writer.
This gate prevents falsely treating all labels as external unresolved symbols.
The exact-head grouped change requests **only seven cached LoongArch Linux
shards** via [minias-replay-loong-7], not three GNU Linux recompiles nor 21
full scans. The workflow watches its GNU oracle test script so this requested
batch actually runs. Remaining major blocker families must be measured from
the new exact-head aggregate, not inferred from earlier counts.

## Grouped GNU LoongArch64 blocker fixes: expression/alias/ALSL

The original Linux 6.6.143 seven-way Pareto (run 40473773342) showed
`lu12i.w` 791, `bgtu` 332, `bleu` 209, `ble` 149 and
`alsl.d` 137 FIRST blockers. This batch implements these shared root
causes **before** the next frozen-corpus scan: one Common signed constant
expression evaluator reuses the RV-established grammar but selects arithmetic
negative right-shift semantics (unsigned callers unchanged), the four
LoongArch relational GNU aliases lower to existing B16+symbol fixups, and
`alsl.w/wu/d` uses the GNU LA instruction-table sa2=(shift-1) bitfield.
The Target is responsible for encoding; Common remains responsible only for
expression semantics, layout and ELF RELA. The exact-source GNU oracle now
checks positive and rejected boundary cases. No Linux compiler rerun.

## Complete GNU base integer / indexed LS / atomics-first-blocker batch

After previous grouped real Linux replay, the exact 7-shard Pareto was
`LDX.D 491`, `AMADD_DB.W 194`, `LDX.BU 147`,
`REV B.2H 98`, plus other same-shape indexed LD/ST, bit-twiddling,
LL/SC, CSR and MULH/DIV forms. The *same* Target register-register encoder
has been extended from verified GNU binutils opcode tables (0x380x/381x
for LDX/STX, 0x3869..386d for DB atomics, 0x0000x fixed unary,
0x002x for mul/div, 0x040x for privilege), with the atomic register
field permutation explicitly Target-owned. The pre-existing signed-14
scaled-offset family is reused for LL.W/D and SC.W/D. The GNU exact-byte
oracle now includes these instruction families and fail-closed CSR ranges.
No x86/A64/RV backend copies and no new ELF serialization.

## Second Loong family data recovery: GNU pseudo-atomic 4th zero + ELF ADD/SUB

The next complete GNU Linux 2742-file census (run 40475374482) reached
953 assembled-unverified objects. Its leading **new** first blockers were
AMADD_DB.W 397 and AMOR_DB.D 221, all genuine GCC/GAS 4-operand forms
`amadd_db.w $rd,$rj,$rk,0` rather than a new hardware encoding.
The earlier target accepted the 3-register hardware mnemonic but incorrectly
rejected the GNU macro fourth `u0:0`. Both forms now map to **one target
opcode table** and fail closed unless the macro's reserved fourth operand
is exactly zero. Expanded DB and non-DB atomic families reuse the same
permutation (assembler operand2 in bits10, operand3 in bits5), verified
against GNU real bytes.

A separate 430 blocked objects reported `unsupported-symbol-difference-width`
4/8 because Loong Target never described `R_LARCH_ADD/SUB32/64` despite
Common already having mature RV-style symbol-minus-dot relocation plumbing.
Mapping ADD32=50 SUB32=55 ADD64=51 SUB64=56 at the Target ABI boundary
makes this existing Common facility reusable without a second Loong fixup
implementation. A genuine GNU `.long/.quad external-.` oracle checks
the four specific relocations. Only affected seven frozen Loong shards
run after the grouped fixes, never new GNU C compilation.


## Target/Common architecture audit: symbolic x86 memory immediates

The x86 GNU target now retains symbolic `movq $imm32, disp(%base)` with
`MiniAsMCInst`'s typed symbol operand. Its existing ModR/M/REX/SIB path
produces machine bytes; the existing `MINIAS_FIXUP_X64_ABS32S` and Common
ELF writer produce relocation. This does **not** introduce an ISA switch in
Common or a duplicate ELF implementation. A symbolic immediate **and**
symbolic memory displacement within one instruction remains fail-closed
until an exact GNU two-fixup contract exists.

Structural audit debt (not silently declared solved): x86 target encoding
still carries target-private operand-width flags; refactoring these into an
x86-only descriptor must not change MCInst/ELF/layout ownership. Common
`.comm`/SHN_COMMON and forward-dependent `.skip` need a symbol/layout
solution shared by every ISA, not x86-specific exceptions. Frozen Linux
assembly discovery cannot establish GNU object parity or a linked Linux P1.


## Common .comm ELF architecture contract

A GNU `.comm` is a semantic `SHN_COMMON` symbol, with `st_value` equal
to alignment and `st_size` equal to the tentative size, not a fake BSS
section allocation. Explicit `.local` before `.comm` creates real BSS via
Common alignment/data fragments while preserving the caller's current
section. Duplicate global COMMON declarations retain first size and maximum
alignment (GNU GAS behavior). x86 GNU's implicit alignment is recorded in
the TargetInfo extension rather than hard-coded in the Common parser;
other targets reject unverified default alignment rules. GNU `readelf -Ws`
parity checks guard section index, binding, size, alignment and local BSS.
No new ELF backend or target-specific symbol table. The forward-dependent
`.skip` issue still requires a separate correct layout solution.


## x86 Group-2 ISA descriptor audit

The old implementation duplicated shift mnemonic/width lists for GNU's
implicit-one and two-operand forms, and only supported 32/64-bit sizes.
One x86 target-local descriptor now maps SAL/SHL/SHR/SAR/ROL/ROR across
8/16/32/64 widths to the existing shift MCInst, with one measure and
one emit path handling D0/D1/D2/D3/C0/C1, 66 override and REX.
Legacy AH/CH/DH/BH are **not** SPL/BPL/SIL/DIL: TESTB immediate uses a
separate target-local no-REX classification, while unimplemented high-byte
forms remain explicitly rejected. GNU exact bytes test both positive and
negative forms. Common, RV, A64, LoongArch and ELF serialization do not
change. These additions do not claim complete GNU Linux object parity.


## x86 system / Group-3 / bit operations, architecture audit

Previously separate first-failure roots (MUL/DIV/IDIV, BSF/BSR,
BSWAP, BT/BTS/BTR/BTC, CMPXCHG, RDMSR) now reuse retained target MCInst
and existing ModR/M/REX/SIB, PC-relative fixups and Common ELF path.
Unary Group-3 /4..7 uses the existing register/unary-memory pathway,
with no new memory encoder. Register-register two-byte instructions have
Target-local 0F escape and operand-direction flags; BSF/BSR swap REG and
RM fields while bit operations keep the ordinary AT&T reg->r/m layout.
Exact GNU AS machine-byte comparison and invalid width contracts gate the
entire ISA family. No ISA-specific handling enters Common.

Architectural debt explicitly tracked: target opcodes, mnemonic aliases,
and width flags still live inside x86_64_target.c. A later Target-internal
format descriptor can replace that representation without touching
MiniAsTargetOps, MCInst, layout or the shared ELF writer.


## x86 GNU suffix-inference and TEST regression, 2026-10-27

GNU AT&T source permits e.g. `add %gs:symbol(%rip), %rbx`, whose
register operand unambiguously specifies x86 word size. The existing
ISA-owned reg/memory opcode table is reused with a target-local mnemonic
suffix inference rule; the Common grammar remains architecture-neutral.
Group-3 TEST register immediates are now handled by one typed target opcode
with A9 for accumulator and F7/0 elsewhere, complete with the original
MCInst layout and GNU exact bytes. The x86 Target retains responsibility
for REX.W, 66, register width and numeric range rejection. No duplicate
ModR/M encoder, ELF writer or ISA leakage into Core.


## GNU x86 conditional move / bit scan from memory

CMOVcc and BSF/BSR memory-source instructions now reuse the *same* typed
MCMemory operand, address-space/REX/SIB form and GNU PC32 symbol fixup as
ordinary MOV/ALU memory operands. The x86 Target resolves its own condition
code or bit-scan mnemonic and supplies a two-byte opcode; Common performs
no condition decoding and no architecture-specific relocation rewrite.
Both suffix-free GNU AT&T widths and explicit w/l/q are guarded by the
register's actual class. GNU byte and relocation comparison rejects wrong
operand direction. This is an ISA expansion, not a new backend pipeline.


## x86 GNU per-CPU absolute indexed SIB and ISA blocker convergence

The Linux GNU `symbol(,%index,8)` form is NOT RIP-relative. It uses an
absolute disp32 SIB displacement with R_X86_64_32S, and is now recorded in
the **same** typed MCMemory/SymbolExpr operand used by the RV-proven Common
fixup/ELF pipeline. The x86 encoder emits a *typed* ABS32S fixup at the
actual encoded displacement offset instead of silently dropping the symbol.
Real GNU AS machine bytes and specific `__per_cpu_offset` relocation counts
are required gates. PUSH/POP r/m64 also reuse existing ModR/M without a
spurious REX.W, while GNU Group-1 ADC, legacy DH immediate and fixed
CLTD/SFENCE/MFENCE/LFENCE instructions are target-only ISA extensions.
No new public target interface, Common ISA switch or duplicate ELF writer.

GNU forward-dependent `.skip` and cross-section symbol differences remain
blocked pending correct deferred expression/ELF relocation contracts; we
must not make unresolved expressions "work" by fabricating a zero size.

## GNU Linux ISA delta + architecture boundary (2026-10-27)

The x86-64, AArch64, and LoongArch64 Linux-first additions remain three
parallel **Target ISA** extensions on the unchanged Common assembler pipeline:

- **x86-64**: GNU XCHG 8/16/32/64 memory/register forms reuse the retained
  typed register/memory ModR/M, SIB, segment-prefix and PC32/ABS32S
  relocation logic. IN/OUT with DX and the implicit accumulator is validated
  against the *actual* mnemonic suffix; the emitted 1- or 2-byte target
  instruction remains an MCInst, not an assembler-global prefix state.
- **AArch64**: CCMP/CCMN share the existing condition-code grammar and a
  single typed four-operand conditional-compare format. EL1 TPIDR/DAIF
  MRS/MSR encodings extend the target-only *named* system-register table,
  not a permissive arbitrary register escape; YIELD/WFE/WFI/SEV/SEVL reuse
  the existing typed HINT immediate implementation.
- **LoongArch64**: LASX XVPICKVE2GR supports **only W/D/WU/DU**; upstream
  GNU binutils loongarch-opc.c confirms 0x76efc000 / 0x76efe000 /
  0x76f3c000 / 0x76f3e000 with unsigned lane ranges u10:3/u10:2.
  The tempting LSX-derived B/H analogues are not real LASX mnemonics and
  must fail closed. Table data are ISA-owned, machine instruction layout
  and ELF emission still use the shared implementation.

Exact GNU AS instruction bytes and relocation identity are required for
the x86/Loong oracle; A64 instruction bytes, GNU LD and QEMU have their
own independent contract. A successful 3x7 frozen original GNU Linux
6.6.143 *S* scan is only ASSEMBLED_UNVERIFIED and cannot establish GNU
object semantic parity or a working Linux Image/P1. Neither branch marker
nor a passing discovery-workflow shell is a substitute for its aggregate
count and recorded exact source HEAD. RISC-V mature acceptance remains a
regression control, not a reason to clone Common logic into a target.

Outstanding architectural debt: deferred/relocatable GAS expressions,
full symbol difference semantics, .eh_frame, and relaxation/target option
profiles. Correct implementation belongs at their existing Common owners
with ISA-specific syntax and fixups solely in Target. Never fake .incbin
inputs to manufacture an assembly success.
