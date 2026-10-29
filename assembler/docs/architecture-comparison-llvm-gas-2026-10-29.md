# MiniAS architecture cross-audit: LLVM MC and GNU GAS (2026-10-29)

**Scope / audit pin.** Branch `refactor/minias-target-isa-abi-core`,
head `0ddb47a99817d753d6d92c3fe769e0880b3a00d0`.
The recursive Git tree enumerates **35** production MiniAS/shared-ELF
C/header files (approximately **30,735 lines** at this head).
This is a **source-architecture audit**, not a declaration that all original
Linux GNU object files, final Image links, or QEMU P1 were certified.
No additional production changes are implied by this document.

We re-inspected the complete production-file inventory, module boundaries
and selected high-risk paths, including fallback, failure cleanup and
backward compatibility. A mechanical source scan is not a substitute for
independent line-by-line code review of every branch. The pre-existing
`full-source-review-ledger-2026-10-29.md` records the earlier six-domain
full read and previous verified repair batch; this document provides a
**new, evidence-based cross-project comparison** without recycling that
claim as a fresh independent six-agent review.

## A. Actual MiniAS ownership graph

```text
CLI (tools/minic-as/main.c)
  -> public Invocation (assembler/src/invocation.c)
  -> target_registry: single name -> TargetOps / TargetSpec
  -> assembly session (assembler.c / MiniAs)
       -> immutable physical source frames / macro expansion site IDs
       -> symbols, SectionKey -> SectionID, subsection index
       -> persistent MCStmt (fragment kind + MCExpr + MCInst + site state)
            -> Target parse_mcinst: ISA operands / symbol refs
            -> Common MCLayout: subsection concatenation, anchors,
               .org, variable DATA, alignment, relaxation fixed point
            -> Target plan_instruction_relaxation / machine code + fixups
       -> emission
            A: instruction and executable padding -> EncodeResult commit
            B: DATA/FILL/SPACE and ordinary padding -> direct Section append
       -> CFI/DWARF emitters -> Common ELF object planning
       -> minielf_build_relocatable -> write output path directly
```

**TargetOps** is one registry for RV32/RV64 (shared RISC-V backend),
x86-64, AArch64 and LoongArch64. Core source contains no hard-coded
ISA-specific conditional dispatcher. Backend C files contain no calls
to `minias_section_append` / `minias_get_symbol`; machine fixups
are staged through the semantic Common transaction. This is a
meaningful existing architecture strength, not a proposed refactor.

## B. What LLVM and GAS actually do

| Area | LLVM MC evidence | GNU GAS evidence | MiniAS current assessment |
| --- | --- | --- | --- |
| Parse/stream | `MCStreamer`, `MCObjectStreamer`, `MCContext`; ObjectStreamer retains section state and pending labels | `read_a_source_file`, pseudo-op tables and target `md_assemble` | Public Invocation/Target registry decoupling is sound; `MiniAs` lifetime is too broad |
| Fragment | `MCFragment` typed forms: data, align, fill, org, relaxable; every fragment has a section owner | `fragS` (fixed/variable tail), `frchainS` per subsection, `frag_more` | Typed Fragment and MCLayout exist; direct-data append remains a parallel commit path |
| Relax/fixup | `MCAsmBackend::fixupNeedsRelaxationAdvanced`, `relaxInstruction`, `MCAssembler::relaxOnce`; writer records late relocation | `md_relax_frag`, `relax_segment`, `fixup_segment` and target `md_apply_fix` | Monotonic typed target plans are a strength; conditional-dependent replay and RV linker RELAX/ALIGN are incomplete |
| ELF sections | `MCContext::getELFSection` interns by (name, group, unique ID, linked symbol) | GNU `.section` can use `unique,<number>` and group inheritance `?` | MiniAS indexes (name, COMDAT signature, LINK_ORDER anchor) only; missing unique identity and inheritance |
| Instruction grammar | TableGen `AsmMatcher` generates target matcher from instruction records; target custom parsers remain | Each target implements ISA grammar in `tc-*.c` | x86/LA typed tables are improving; A64 and RV still carry larger mnemonic-specific chains |
| Output | `MCObjectWriter` owns post-layout bindings and output | GAS passes materialized frags/fixups to BFD | MiniAS minielf library reuses a single object serializer across parser/objcopy/reader consumers (strong); direct final-path write is not atomic |

Official reference implementation/documentation:
- LLVM `llvm/docs/CodeGenerator.md`: https://llvm.org/docs/CodeGenerator.html
- LLVM `MCAssembler.cpp`: https://llvm.org/doxygen/MCAssembler_8cpp_source.html
- LLVM `MCContext.cpp` section-uniquing key: https://llvm.org/doxygen/MCContext_8cpp_source.html
- LLVM TableGen AsmMatcher: https://llvm.org/docs/TableGen/BackEnds.html
- LLVM RISC-V backend linker-relax/align:
  https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/RISCV/MCTargetDesc/RISCVAsmBackend.cpp
- GNU GAS internals (`fragS`, `frchainS`, `md_assemble`, relaxation):
  https://gnu.googlesource.com/binutils-gdb/+/00d962d155e1cff6ce144f60f81a416e0f9b872e/gas/doc/internals.texi
- GNU GAS `write.c`, `relax_segment`, `fixup_segment`:
  https://gnu.googlesource.com/binutils-gdb/+/6c5eabaf45bed70acc2e64d8846ccb2eb0752e7e/gas/write.c
- GNU GAS ELF `.section` semantics: https://sourceware.org/binutils/docs/as.html

**Qualification:** The GNU assembler is part of GNU binutils, not
GCC's internal codebase. GCC normally emits assembly and invokes an
assembler; compare MiniAS directly with GAS and LLVM MC rather than
mistaking a GCC code-generation implementation for an assembler.

## C. Confirmed findings (ranked; not all are runtime defects)

| ID | Priority | Evidence in pinned MiniAS | Problem / boundary | Closure condition |
| --- | --- | --- | --- | --- |
| A01 | **P0 architectural** | `assembler.c:796-880,883-931,983-1065,6486-6659`, `mc_encode_result.c:1-137` | Instruction/code-alignment bytes and fixups are staged, but DATA/FILL/SPACE and plain alignment append directly. `capture_instruction_bytes` / `emitting_fragment` are bridge state. This violates *one fragment, one commit protocol*, though assembly failures currently suppress normal ELF output. | Introduce a Common `FragmentEmission` transaction/sink for every fragment, with distinct byte, fill, pattern and NOBITS operations; preserve explicit section-owned `.reloc` and synthesized DWARF fixups. Remove old append path only after migration and GNU comparisons. |
| A02 | **P1 ABI identity / confirmed missing** | `assembler.c:267-~440, ~1390-~1600`, `minias_internal.h:50-75,474-490` | SectionKey identity omits GNU ELF `unique,<number>`. The parser rejects trailing `unique` arguments. LLVM interns a quadruple, not just MiniAS's triple. | Add optional unique ID to an immutable SectionKey type, parser, hash, stack restore, ELF map and tests. Include uniqueness and same-name/different-entsize GNU oracles. |
| A03 | **P1 silent acceptance** | `riscv_target.c:448-495`, `riscv_encode.c`, `riscv_target.h` | GNU `.option relax` and `.option norelax` are accepted in some modes but have no option-state branch, and no `R_RISCV_RELAX`/`ALIGN` emission policy. Accepting the spelling without its object semantics can yield a silently different link contract. | Either implement per-site relax state and linker-relax-safe fixup/ALIGN policy with GNU ELF order comparisons, or reject unsupported mode explicitly. Keep RV32I strict ISA constraints distinct from linker relaxation. |
| A04 | **P1 lexical state** | `assembler.c` `.section` parser `~1400-1550`; `MiniAsSection` | GAS's `?` flag inherits the previous section's COMDAT group. MiniAS rejects it as unknown. This is different from explicit `G` and must bind source context at parse site. | Model effective section metadata from preceding section before key lookup, don't add `?` as a persistent ELF flag. GNU verify stack/pushsection and explicit vs inherited COMDAT. |
| A05 | **P1 deferred expression duplication** | `aarch64_target.c:~2280-2410` (`a64_parse_deferred_difference`, `a64_attach_branch_target`), `mc_expr.c` | Target manually splits parens, +/- and symbol differences for branches. It preserves semantics for tested cases but duplicates Common expression-tree rules and has a separate grammar drift surface. | Common produces retained MCExpr (symbol-difference/base + checked absolute suffix); Target owns only reloc eligibility/bitfield encoding. Delete manual string decomposition after exact GNU positive/negative oracles. |
| A06 | **P1 rare source replay cost** | `assembler.c:3905-3970,4503-4825,6661-6749`; `mc_layout.c:1085-1149` | `layout_sensitive` is a coarse boolean. When branch growth affects parser control, a new `MiniAs` is built and parsed; stable source identity only prevents wrong ordinal rebinding, not full replay. | Capture conditional read dependencies and generation-checked symbol/fragment edges. Keep a correct conservative fallback until a dependency test matrix proves substitution safe. Do not weaken current fail-closed shape checks. |
| A07 | **P1 failure atomicity** | `elf_writer.c:~509-541` | After building a complete image, `fopen(path,"wb")` truncates the destination, then `fwrite/fclose`; a write/close error can leave a partial output, or destroy a prior valid one. Earlier parse-failure no-partial-object tests do not cover disk output errors. | Same-directory temporary output, close/error verification and atomic replace; cleanup on failure. Separate OOM/data-buffer tests from disk-write tests. Consider platform-specific flush/durability requirements separately. |
| A08 | **P2 interface width** | `minias_internal.h:~474-574,~780-865`, TargetOps callbacks | Target callbacks retain a mutable `MiniAs *`, giving parse/encode/full output phases access to nearly every Common resource; `register_64` and `target_reg_bank` mix generic bits and ISA-private meaning. | Establish phase-specific `TargetParseView`, `TargetLayoutView`, `TargetEmitSink` with opaque IDs. Use generic operand width + private register bank while preserving actual ABI width constraints. |
| A09 | **P2 opcode matcher shape** | `aarch64_target.c:~2400-4040`, `riscv_encode.c`; `x86_64_target.c`; `loongarch64_target.c` | A64/RV contain many direct mnemonic comparisons; **the number of `strcmp` calls alone is not a defect**. The architectural debt is a possible mismatch between duplicated parse/measure/encode form descriptions or Target-only fallbacks. | Inventory instruction families and locate *actual duplicate* form-selection decisions; convert coherent families to one Target descriptor table, not to a generic all-ISA encoder. Use GNU wrong-width, near/far, GPR/FPR and feature-gate oracles. |
| A10 | **P2 repeated primitives** | `cfi_writer.c:14-64`, `debug_line.c:201-245`, `elf/src/{relocatable_writer,rewrite}.c` | Repeated little-endian stores, ULEB/SLEB output, array growth. However CFI/line programs and writer/rewrite have different semantics; combining the serializers would be an architectural mistake. | Only share checked buffer/varint primitives where consistent; keep each stream's state machine and ownership separate. |
| A11 | **P2 compatibility bridges** | `assembler.c:1098-1107`, `minias_internal.h:~495-508`, `mc_operand_semantics.c:30-57` | Legacy RISC-V public init and capacity/count fallback coexist with newer TargetSpec and owned MCOperand paths. Some are public compatibility contracts, not dead code. | Identify actual consumers by symbol/link API, migrate call sites, delete unused private bridges; retain promised public APIs or version them. |
| A12 | **P2 fixed token ceilings** | `minias_internal.h` `MiniAsSymbolExpr.name[256]`, A64 parser local `tok[4][384]`, LA typed split with overflow allocation | Target operand capacity strategies differ, and one Backend has a dynamic large-operand path while others have fixed buffers. Not proof of bug until real GNU inputs fail. | Inventory size-limited APIs and bound checks against original Linux inputs; introduce shared ownership-aware token views without changing ISA dialect parsing. |

### New reproducible GNU positive probes (manually executed, x86-64 host)

**Unique section identity**:
```asm
.section .text.same,"ax",@progbits,unique,1
.byte 0x11
.section .text.same,"ax",@progbits,unique,2
.byte 0x22
.section .text.same,"ax",@progbits,unique,1
.byte 0x33
```
`as --64` emitted two physically distinct `.text.same` Sections,
size 2 (`11 33`) and size 1 (`22`). MiniAS's current parser lacks
the `unique` argument, so **MiniAS parity for this input is NOT TESTED
as PASS**.

**Group inheritance**:
```asm
.section .foo,"axG",@progbits,g1,comdat
.byte 1
.section .bar,"ax?",@progbits
.byte 2
```
`as --64` emitted `.foo` and `.bar` as members of the same `g1`
COMDAT group. MiniAS does not parse `?` yet.

**Distinct mergeable sections**:
`as --64` accepts two `.s` sections with
`"aMS",@progbits,1,unique,3` and
`"aMS",@progbits,2,unique,4`. Their distinct `sh_entsize`
values are 1 and 2. This confirms unique section identity is not
merely a performance or parser nicety.

### Reasonable ISA specializations to *retain*

- x86 REX, high-byte GPR, SIB/ModRM, XCHG accumulator short spelling,
  PC32/PLT32 and symbol-preemptibility are **Target-private ISA/ABI**,
  not Common hacks.
- A64 memory addressing, condition codes, exclusive/LSE operations,
  literal-pool ordering and CFI personality are **Target policy**;
  the Common layer should not model these encodings.
- RV XLEN, ISA extensions, RVC lexical state and ELF RISC-V relax flags
  belong to one RISC-V Target with RV32/RV64 feature/profile variation.
- Loong GPR/FPR/FCSR/FCC/LSX/LASX banks, PCALA relocations and
  B16/B21/B26 forms are genuinely ISA-private.
- `word_directive_width`, `align_directive_bytes`, CFI ABI
  and named relocation hooks are **appropriate declarative TargetOps
  differences**; they do not require a Common `switch(target)`.
- Separating `.reloc` (section-owned) and instruction Fixups
  (fragment-owned) is intentional and correct. One commit protocol
  must not erase this distinction.

## D. Existing strengths and risks vs. larger assemblers

**Advantages in this project's target scope**:
1. Frontend/CLI need not enumerate architecture names; the target
   registry owns descriptors. RV32 and RV64 share one meaningful backend.
2. Stable SymbolNameIDs/SectionIDs, retained MCExpr/MCInst and
   fragment-local fixup offsets give a robust basis for late relocation.
3. Typed operand memory ownership and fail-closed unsupported Target
   forms reduce accidental wrong-code risk in a growing custom toolchain.
4. The Common MCLayout fixed-point model, GNU-comparison probes, A0
   regression and exact-head CI create unusually explicit evidence
   for this project's four supported backend families.
5. A compact reusable MiniELF writer/reader/rewrite library supports
   independent toolchain programs; avoid merging all consumers into an
   assembler-only writer.

**What LLVM/GAS have and MiniAS does not yet**:
1. Fully persistent fragment output and clean late layout/fixup phases.
2. A more complete ELF Section identity and GNU source directive grammar.
3. Mature link-relaxation, architecture feature/profile and relocation
   semantics, including target-specific late edits.
4. Broad table-based instruction grammar and negative-form coverage.
5. Mature object output APIs, failure handling, and format integrations.

**Non-goals / avoid false optimization**:
- Do not copy LLVM's object-format polymorphism into an ELF-only project
  without a real Mach-O/COFF requirement.
- Do not blindly merge CFI/DWARF/debug serializers.
- Do not use a shared x86/A64/Loong instruction encoder; reuse
  *interfaces*, not instruction semantics.
- Do not count fixture-only CI green as complete original GNU object,
  final Image, or QEMU kernel-boot parity.

## E. Recommended change order (big coherent batches)

**Batch 1: strengthen ELF SectionKey correctness.** Treat as one
subsystem: add nullable explicit `unique_id` (and effective inherited
COMDAT flag on parse) to SectionSpec/SectionKey, section creation,
stack/previous/current subsection handling, ELF planner and complete
GNU `unique`/MERGE/LINK_ORDER/? positive+negative tests. Verify
4096-section stress still passes. No separate fake SectionKey with
name-only indexing. This is newly exposed and bounded in scope.

**Batch 2: Common fragment emission transaction.** Introduce a
Common-owned `FragmentEmission` proposal for bytes, `fill`,
`pattern`, NOBITS size and fragment-local fixup requests. Commit
after exact `stmt->size` validation; update Section and Fixup once.
Keep `.reloc`/CFI as explicit *section-owned* distinct fixups. Delete
the old bypass path in the same batch. Test A0, all target GNU,
large .fill, .space, .org, CFI, NOBITS/zero and fail-closed OOM.

**Batch 3: RISC-V RELAX/ALIGN *contract*.** Before adding any
nontrivial extension, either return explicit unsupported errors for
`.option relax` or implement target-owned per-site policy + ELF
`R_RISCV_RELAX` and `ALIGN` handling. Compare exact GNU offset,
type, symbol and addend in RV32I/RV64/optional RVC profiles.

**Batch 4: SourceGraph and Target forms.** Replace coarse replay
with dependency-aware conditional snapshot graph only after atomic
Fragment model stabilizes. Then remove A64 deferred-expression string
subparser with Common persistent expressions and prune genuine
A64/RV instruction-form duplicate decision points. Clean TargetOps
ownership and file lifecycle after callers have migrated.

**Cross-cutting signoff**: produce exact-head Release -Werror/A0,
all four Target GNU positive and invalid-form tests, ELF
reader/objcopy/link, original Linux scoped source matrix, and
independently recorded Image/QEMU P1 when those phases are attempted.
A GNU cross tool absent locally remains **NOT TESTED**, never PASS.

## F. Current audit status

- **Proven at preceding exact HEAD** `0ddb47a9`: MiniAS
  ISA/ABI run **40893522795** SUCCESS; x86/Loong GNU Oracle run
  **40893522848** SUCCESS. The source audit does not transfer those
  results to any hypothetical code change.
- **New findings** A02/A04 were reproduced using host GNU `as`.
  A03 confirmed by inspecting the directive branch and the lack
  of option-state/ELF-relax handling; full GNU RV relocation
  comparison **not performed in this audit**.
- **Documentation-only**: no production fix was applied in this audit.
  Do not represent the above as complete architecture cleanup.


## G. Follow-up full-inventory contract audit (2026-10-29, source head e9a53684)

This section is a **separate follow-up** to A01–A12 above, not a new
claim of six independently executing subagents. The GitHub source at
`e9a53684ea75c0e713a7a55dcea4142aeac21df2` was fetched for each
of the same 35 production files. Source inventory counts at this exact
head total **30,735 lines**: Common source 7,465 (4); MC model 4,265
(8); RV 3,102 (6); x86 3,630 (2); A64 5,257 (2); LA 2,030 (2);
CFI/ELF/shared object tools/CLI 4,986 (11). The 35-file repository
scan included function-level architecture metrics and a targeted manual
re-read of high-risk producer/consumer call chains; the mechanical scan
itself does **not** imply 30,735 branches were manually validated.

### G.1 One newly confirmed GNU Section metadata discrepancy

**A13 / P1 / confirmed source-path discrepancy**:
`assembler/src/assembler.c:1410-1598` parses `S` and sets
`MINIAS_SHF_STRINGS`, but only parses/assigns `entry_size` when
`MINIAS_SHF_MERGE` is present. `elf_writer.c:282-305` copies
`input->entry_size` into the emitted object without supplying a
default. GNU GAS defines `S`'s `entsize` as default **1** even when
the `M` flag is absent. GNU host assembler reproduced:

```asm
.section .rodata.strings_only,"aS",@progbits
.asciz "hello"
.section .rodata.merge_strings,"aMS",@progbits,1
.asciz "world"
```

Host `as --64` + `readelf -SW` printed `sh_entsize=1` for both
sections. MiniAS output was **not executed** for this probe in this
follow-up; the MiniAS control-flow assignment implies the single-S
section retains zero, pending actual exact-head GNU object comparison.

Fix inside the **whole SectionSpec/SectionKey batch**, not a standalone
`if (S) entsize=1` patch. Its invariant is that the final effective
flags, type, entsize, group and unique identity are computed once before
interning. Also audit `S`-only explicit entsize, `E` and `M`
combinations, and GNU first-declaration behavior.

### G.2 GNU syntax omissions: classify by severity

Host/current GNU `.section` documentation describes `unique,<number>`,
`?` inherited grouping, `+` / `-` attribute inheritance, standalone
`E` entry size, numeric flags/types, and `.attach_to_group`. Current
MiniAS `handle_section` accepts `a,w,x,M,S,T,G,R,o`; unsupported
characters fail explicitly, and trailing `unique` is rejected. Most
are **explicit missing coverage**, not proven wrong-code bugs. Treat
`unique`, `?` and the `S` default as first-priority actual GNU
Section identity/semantics deficiencies; prioritize the rest according
to unmodified compiler-generated sources, avoiding unbounded
compatibility work.

References:
- https://www.sourceware.org/binutils/docs/as/Section.html
- https://llvm.org/doxygen/MCContext_8cpp_source.html
  (ELF identity quadruple: section name, group, unique ID, linked symbol)

### G.3 Actual cross-layer split vs. legitimate target specificity

| Producer and consumer | Observed invariant | Judgment |
| --- | --- | --- |
| `assembler.c:6486-6659` + `mc_encode_result.c` | instruction and executable ALIGN stage bytes/fixups; DATA/FILL/SPACE use `minias_section_append*` directly | **Architecture debt**: one fragment-wide proposal/commit protocol; retain lazy large-fill representation and NOBITS semantics |
| `aarch64_target.c:2208-2308` + `mc_expr.c` | A64 scans source string for parenthesized `symbol+(symbol2-symbol3)+const` while Common already retains signed MCExpr / bound differences | **Real second grammar**: migrate semantic expression parse to Common, target only decides address/relocation legality |
| `riscv_target.c:440-496` + `riscv_target.h` | explicit `.option relax` passes strict-ISA whitelist, but target directive body only mutates pic/nopic/rvc/norvc/push/pop | **Semantic silent gap**: must implement true per-site linker relaxation contract with R_RISCV_RELAX/ALIGN or reject unsupported mode |
| `minias_internal.h:474-590,787-873` | TargetOps callbacks take mutable `MiniAs*` covering parser/symbol/fixup/ELF/CFI state; Target site uses opaque 32-bit mode bits | **Interface debt**, not proof of current overwrite: phase-specific views/sink after emission unifies |
| `elf_writer.c:504-541` | complete ELF buffer is directly `fopen(path,"wb")`, then fwrite/fclose | **Failure-atomicity gap**: parse/OOM failures do not emit, but disk I/O errors may truncate an existing file; verify temporary-output + atomic replace |
| `x86_64_target.c` REX/SIB/high GPR, `aarch64_target.c` A64 memory/literal, `riscv_encode.c` XLEN, `loongarch64_target.c` register banks | machine-specific form and fixup bitfield decisions stay Target-local; no target source directly calls `minias_section_append` | **Correct specialization**, do not generalize their machine encoding into Common |

LLVM source references checked:
- https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/include/llvm/MC/MCObjectStreamer.h:
  `allocFragSpace`, `appendContents`, `addFixup`, `emitInstruction`;
- https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/include/llvm/MC/MCAsmBackend.h:
  `applyFixup`, `fixupNeedsRelaxationAdvanced`,
  `relaxInstruction`, `relaxAlign`;
- https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/MC/MCAssembler.cpp:
  `MCAssembler::relaxInstruction` uses retained MCInst/fixups, rather
  than replaying source mnemonics for ordinary relaxation;
- GNU GAS internals and GNU source syntax:
  https://gnu.googlesource.com/binutils-gdb/+/00d962d155e1cff6ce144f60f81a416e0f9b872e/gas/doc/internals.texi
  and https://sourceware.org/binutils/docs/as.html.

### G.4 Actual strengths and migration constraints

- **Advantage**: a compact, pluggable four-backend Common MC engine,
  one explicit Target registry, stable Section/Symbol IDs and site
  identities, Common-owned relocation bookkeeping, and one shared
  MiniELF serializer. The earlier 4096-section/8192-subsection GNU
  stress and four GNU target gates are real evidence of this substrate.
- **Advantage**: ISA-specific features remain private to Targets, while
  byte/fixup offset finalization is Common-owned; fail-closed unsupported
  forms are preferable to unverifiable machine code.
- **Do not clone** LLVM's all-format object-streamer polymorphism or
  GAS's mutable global parser state. GAS is the assembler in the GNU
  toolchain; GCC normally emits assembly and invokes a separate
  assembler, so compare GCC-generated `.s` input compatibility to GAS,
  and the MiniAS ownership architecture to LLVM MC.
- **Release ordering** remains SectionSpec/SectionKey semantic identity
  (including unique/inherited group/default entsize), then unified
  Fragment emission, then the RISC-V linker-relax semantic contract,
  then incremental SourceGraph and demonstrated Target matcher
  duplication. This is a whole-subsystem migration plan, **not** proof
  any of these unresolved P0/P1 items were fixed.
- The latest architecture note changes **documentation only**. No
  Release/A0/GNU/Image/QEMU success is ascribed to its commit; earlier
  exact-head green workflows apply only to the recorded earlier SHA.
