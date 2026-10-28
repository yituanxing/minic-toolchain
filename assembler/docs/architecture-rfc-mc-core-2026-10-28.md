# MiniAS MC architecture RFC — responsibilities before refactoring

Status: **proposed design / architecture audit, not implementation**. Baseline: PR #667, commit `8c705d2fd5803756b2058ab9a29ebe4dee233595`, 2026-10-28.

## 0. Decision and non-goals

**Decision:** model the assembler *first*, then adapt four Targets to the model. Do not derive each Common API from the last architecture's first missing instruction. Borrow LLVM MC's ownership and typed-event model, GAS's established GNU syntax/fragment/fixup compatibility; do **not** clone either project's macro/global-state implementation. This RFC makes no machine-code changes.

**Architectural acceptance:** adding an unrelated ISA may add Target registration/build files and Target-owned parser/encoder/ABI/relocations, but MUST NOT add new ISA enums, private fixup constants or operand shape checks to Common, ELF serialization, or the CLI. A genuinely new universal object-file capability may legitimately extend Common through a reviewed model change, not hidden Target conditionals.

**Compatibility:** retain four existing backends, RV32+RV64 in one RISC-V Target, current MiniC -> MiniAS contract, Linux frozen GNU-produced .s replay, real ELF and linker tests. No file-moving-only reorganization.

## 1. Evidence: what exists at the pinned head

| Responsibility | Actual source | Current ownership | Architectural observation |
|---|---|---|---|
| CLI & combined -march/-mabi | `tools/minic-as/main.c`, `target_registry.c` | CLI / Target callback | Good: target-neutral selection; static registry is acceptable. |
| GNU macros, parsing, .set/.equ, sections, symbols | `assembler.c` | Common | Good public responsibility, but large lexing/semantic/layout implementations share a mutable MiniAs object. |
| Instruction representation | `minias_internal.h:175-226` | Shared typed MCInst + Target opcode | Good parse-once direction. Shared register_64 and base/index/scale memory assumptions risk leaking ISA shape into Common. |
| Instruction parsing and encoding | `riscv_encode.c`, `x86_64_target.c`, `aarch64_target.c`, `loongarch64_target.c` | Target | Correct ownership; not all targets have equal ISA/ABI feature gates or descriptor coverage. |
| Variable-size fragments / labels | `assembler.c:731-774,4450-4602,4676-4710,7101-7225` | Common | Statement is de-facto fragment, contains source strings, offset, size, target_options. In-place relaxation plus conservative whole-file reparse fallback. |
| Expression | `expression.c` and `assembler.c:1360+` | Common + individual Target parsers | Absolute arithmetic, symbol+addend and symbol-difference are split representations; a persistent, unified relocatable MCExpr is missing. |
| Fixups | `minias_internal.h:99-165`, `assembler.c:487-536` | Core-owned array; Target relocation callback | All ISA-specific FixupKind enumerators reside in Common header. Fixups are flattened to section+offset during emission and validate via ELF relocation-number mapping too early. |
| Target/profile context | `minias_internal.h:315-399,470-545` | Common struct + Target callbacks | RISC-V-only strict_i_xlen/strict_rv64i/isa_features/isa_explicit in shared config and assembly object; mutable option bitmask with fixed stack, only scalar per-statement snapshot. |
| ELF object | `assembler/src/elf_writer.c:66-296` -> `elf/include/minielf.h:114-156` -> `elf/src/relocatable_writer.c` | Adapter + minielf serializer | Good single output pipeline, but section model omits group/signature/link/info and only offers RELA output. ELFDATA2LSB fixed by MiniAS adapter. |
| CFI & debug line | `assembler/src/cfi.c`, `debug_line.c`, `assembler.c:5045-5095` | Common with Target parameters | Non-x86 .cfi_startproc/.cfi_endproc is accepted without frame emission in a fallback path; debug .file without number is ignored; global comparator_as violates reentrant design. |

This is a **source-code assessment**, not proof of every issue on all four architectures. The pinned 3x7 Linux replay proves GNU-source assembly acceptance, not byte-for-byte GNU .o equivalence, correct COMDAT/unwind semantics or QEMU runtime.

## 2. Six ownership domains (logical interfaces, NOT six giant files)

1. **Frontend**: source lexer, macro/rept/conditional grammar, GNU directives, ISA-specific tokenization callbacks. Produces typed events; never chooses ELF relocation numbers.
2. **MC model**: stable IDs and owned lifetimes for `MCExpr`, `MCInst`, `MCOperand`, `MCFragment`, `MCSymbol`, `MCSection`, `MCFixup`, `TargetSiteState`. No ISA names or ELF relocation values in its public definitions.
3. **Assembly engine**: source-event consumption, symbol semantics, sections/subsections, fragment ordering, iterative layout, expression evaluation, fixup resolution, diagnostic/source mapping, CFI event accounting. It calls Target, not vice versa for Common algorithms.
4. **Target implementation**: ISA/ABI profile resolution, register syntax, instruction matching, opcode/operand constraints, encoding, literal pool policy, target relaxation, fixup field semantics, target directives, feature snapshots, target DWARF/ELF policy. Four ISA implementations, **one shared host**.
5. **Object model**: format-neutral semantic sections, symbols, groups, relocations, debug/unwind records; group membership and symbol identities survive until final materialization. No file-byte offsets or ELF section indices in earlier MC objects.
6. **Object writer**: ELF32/64 serialization, section-number assignment, sh_link/sh_info/group construction, symbol sorting and indexes, REL/RELA choice, endianness, machine flags. Target provides policy, not another duplicated ELF writer.

Dependency direction: Frontend emits into an MCStreamer-like assembler sink; Engine holds model and calls the Target's typed services; Target CodeEmitter returns encoded bytes + fixups (it must not independently append arbitrary section bytes or assign global addresses); Object Builder consumes *finalized* MC and Target object policy; Writer consumes the Object Model. These are ownership rules, **not** a claim that all calls form a one-way linear chain.

## 3. Five mandatory object contracts

### 3.1 MCExpr: one grammar, explicit relocation semantics

- Persistent owned expression tree or equivalent typed nodes: constant, symbol ref, current-location anchor, unary/binary, target modifier (opaque to Common).
- Normalize only when legal to `constant`, `S + A` or `S1 - S2 + A`; preserve GOT/PLT/HI/LO or similar target syntax as annotated expression, not textual reparsing or eager ELF R_*.
- `.` is a **fragment anchor**, never an early absolute integer; same-section differences become absolute only when layout is stable; undefined/cross-section differences follow explicit object-format policy or fail closed.
- Common owns parsing of ordinary integer/symbol arithmetic and checked overflow; Target owns ISA operand dialect and modifiers. Source strings can remain diagnostic provenance but cannot be the sole semantic state.
- Lifetime: context owns an expression arena; MCInst and MCFragment contain stable handles. No borrowed pointers into reallocated symbol/statement vectors.

### 3.2 MCFragment: persistent layout unit

- Kinds: Instruction, RawData, Align, Fill/Space, Org, LEB, LiteralPool, CFI/Debug anchors; labels are zero-width anchored positions, not mandatory emitted bytes.
- Fragment belongs to exactly one Section/Subsection; stable identity across all relaxation passes. Record source location, semantic payload, site Target snapshot, estimated and finalized size, final offset only after layout.
- All size-changing directives use typed deferred expressions. Common performs layout, detects non-convergence and overflow; Target returns instruction expansion alternatives/ranges and NOP fill. **Never reopen or re-expand original source just because of relaxation.**
- Invariants: fragment offsets ordered within finalized subsection/section; emitted byte count equals measured final size; growth/non-growth constraints documented per fragment family; layout recomputes symbol/CFI/debug anchors as a single owner.

### 3.3 MCFixup: target namespace, not one global ISA enum

- Generic envelope: owner fragment, intra-fragment byte offset, target-owned kind ID (namespaced by owning Target), field layout/width, semantic MCExpr, source location and PC-relative/overflow metadata.
- Target CodeEmitter emits fixups; Target AsmBackend defines fields, range/alignment checks, applyFixup and relaxation decisions.
- Engine first asks whether the expression can be resolved **at assembly time** under ELF symbol/preemptibility rules; if resolved, apply field bytes; otherwise Object Builder asks Target RelocPolicy for ELF R_* plus addend/REL-vs-RELA rules.
- Preserve machine bitfield semantics: a 20-bit immediate carried inside a 32-bit instruction is not intrinsically a '4-byte-wide absolute relocation'.
- Common kinds for byte-addressed data are allowed; private RV/A64/x86/Loong kinds move under Target. No early `fixup_relocation_type()` test merely to accept a semantic fixup.
- Do not confuse assembler relaxation, RISC-V linker relaxation relocation pairs, and link-time symbol binding: three distinct phases.

### 3.4 TargetSpec / TargetContext / TargetSiteState

- Immutable **TargetSpec** per assembly: architecture, ELF class, byte order, data layout, ABI, machine flags, base ISA and extension capability policy. TargetSpec validation rejects incompatible -march/-mabi; not a Compiler 'generated instruction subset' flag.
- Mutable **TargetContext** per assembly: current .option/.arch/.code32/.code64/register aliases, target directive push/pop state, literal pools and temporary target parser data. Opaque to Common.
- Immutable, internable **TargetSiteState** captured at each instruction/fragment: active mode, ISA feature selection and encoding policy needed later. Parsing, measuring, relaxing and encoding must consult the *same site's* state. Aliases resolved at parse-site or retained under a stable scoped binding.
- RV32/RV64 stay profiles of one RISC-V Target; x86 .code32 within ELF64 does not automatically change ELFCLASS. MiniC's RV64I-only code-generation preference remains **outside** MiniAS ISA acceptance.
- Shared code may ask Target for `DataLayout`, alignment and CFI policies, but cannot downcast `TargetContext` or read RV strict_i fields.

### 3.5 ObjectModel: a semantic object before ELF bytes

- Sections carry `type/flags/alignment/entsize`, and optionally `Group(signature, COMDAT)`, link/info semantics and stable semantic IDs. Section identity must not be only a name if group/uniqueness makes same-name sections distinct.
- Symbols carry binding/type/visibility/definition/section/size and optional group signature identity; temporary labels are removed only after all relocations/CFI/debug dependents are accounted for.
- Fixup finalization creates relocations referencing **semantic symbol/section IDs**, not already-assigned ELF indexes. Object Writer builds SHT_GROUP, SHF_GROUP, signature symbol, sh_link/sh_info, section symbols, REL(A) tables and final indices.
- DWARF/CFI: Common owns directive/state/event serialization framework; Target supplies register numbering, CIE/FDE ABI, return address and pointer encodings; unsupported target forms fail closed, never silently accept .cfi boundaries with no usable frame sections.
- ELF is first supported format; do not design unnecessary Mach-O/COFF implementations now, but keep semantic model independent of ELF header numbering.

## 4. Read-only audit findings / risks, prioritized

**P0 — silent object semantics:** (a) non-x86 CFI boundary-only acceptance (`assembler.c:5045-5095`) can produce incomplete unwind semantics; (b) no explicit SectionGroup/COMDAT in `MiniAsSection`, `MiniElfWriteSection` or relocatable serializer; (c) unknown or conflicting section metadata is not consistently validated. Source acceptance gates cannot mark these as GNU-equivalent.

**P0 — expansion barrier:** common `MiniAsFixupKind` enumerates every ISA; `MiniAsAssemblyConfig` and `MiniAs` expose RISC-V strict/feature fields; `MiniAsTargetOps` mixes parser, target profile, object format, CFI knobs and encoding. A new fifth ISA would change the supposedly common model.

**P1 — semantics and performance:** (a) repeated source interpretation for layout-sensitive directives and some data; (b) `MiniAsSymbolExpr` is limited to fixed 256-byte name + addend; (c) `MiniAsMCOperand` includes register_64 and an x86-shaped base/index/scale memory record; (d) `MINIAS_STMT_ALIGN` and `MINIAS_STMT_DATA` retain text as active semantics; (e) encoding callbacks append directly to Common section buffers, increasing implicit coupling.

**P1 — cross-target correctness:** declared profile and per-statement feature state are not uniformly modeled in all backends; 8-byte DWARF address size is hard-coded for portions of CFI; debug .file metadata and global comparator make the debug unit incomplete/reentrancy-unsafe. Per-target semantic oracle required.

**P2 — ergonomics:** monolithic functions/handwritten opcode switches are a maintainability concern **only insofar as** they duplicate operand schemas, field encoders or extension checks; splitting files is not an architectural milestone.

## 5. Proposed interfaces (conceptual, names not frozen)

    MCStreamer:
      emitSection(section_key)
      defineSymbol(symbol_id, location_anchor)
      emitInstruction(MCInst, TargetSiteState)
      emitData(MCExpr, width)
      emitAlign(alignment, fill_policy, max_skip)
      emitCFI(event)

    Target:
      resolveSpec(triple_or_name, march, mabi) -> immutable TargetSpec
      createContext(TargetSpec) -> target-private context
      parseInstruction(tokens, Context) -> MCInst + site snapshot
      measure(fragment, site) -> size interval
      encode(MCInst, site) -> bytes + fragment-relative fixups
      relax(fragment, evaluated_fixups, site) -> new encoding alternative
      fixupSemantics(kind, site) -> field/range/apply rules
      relocationPolicy(unresolved_fixup, object_format) -> relocation
      objectPolicy(TargetSpec) -> ELF/DWARF attributes

    MCAssembler:
      consumeEvents(stream)
      layoutToFixedPoint()
      resolveFixups()
      finalizeObject() -> MCObject

    ELFWriter:
      serialize(MCObject, TargetObjectPolicy) -> ELF32/64

**Do not implement these all at once.** First establish ownership/provenance tests and eliminate ambiguous contracts; function signatures are intentionally examples rather than premature frozen public ABI.

## 6. Migration batches and proof obligations

| Batch | Scope | Mandatory proof before moving on |
|---|---|---|
| 0 — contract freeze | This RFC, actual dependency/call map, golden snapshot of GNU-source and existing four-target output | No production-code changes. Existing exact-head baselines retained and correctly labelled ASSEMBLED_UNVERIFIED. |
| 1 — durable expressions | Create typed MCExpr and anchors; migrate Common data, .size, .set/.equ, .org, .if with compatibility bridge | Same-section/cross-section, forward refs, overflow, subsection, macro identities; no source reparse in candidate deferred cases. |
| 2 — fragment and context | MCStreamer events; stable fragment IDs; immutable TargetSiteState; target-owned mutable context | Local/external x86 branch growth, RV variable-size LI, .option push/pop, A64 .req/.unreq, x86 .code32/.code64; no offset drift. |
| 3 — semantic fixups | Target-private kinds; CodeEmitter returns bytes/fixups; core fixup resolution + policy | GNU disassembly, bytes, relocation types/addends and linker behavior for four architectures; unsupported kinds fail closed. |
| 4 — object + debug | SectionGroup/COMDAT, Section identity, ELF policy, CFI/Debug boundary completion | readelf -SW/-sW/-rW/-g, GNU ld duplicate elimination, unwind FDE/CIE, no silent CFI acceptance. |
| 5 — backend extension | Instruction descriptors and ISA feature legality per Target without Common modifications | Four-target same-config Linux acceptance + focused GNU .o oracle; new ISA 'dry run' with only Target+registration additions. |

**Each batch**: preserve no-op output on unchanged inputs where already proven, run focused regression first, then grouped exact-head CI. Compare against GNU with identical flags and configs; Linux .s ingestion, byte/relocation identity, GNU-link success and QEMU runtime are distinct acceptance levels. Do not claim equivalence from assembly counts.

## 7. Boundaries to explicitly NOT unify

- Different ISA encoders, x86 address encoding vs ARM/RV fields, Target instruction grammar and ISA extension dependencies.
- General-purpose operand envelope does not require one universal memory-address struct. Opaque Target-owned typed payloads are allowed.
- GNU assembler accepts dialect-specific directives; Common should own directive dispatch and generic semantics, Target their dialect.
- ELF semantic policy and ELF serialization are separate even though MiniAS currently supports ELF only.
- A new CPU is not a new ABI or a new assembler; RV32/RV64 are one target family with distinct profiles.

## 8. Architecture review decision gate

Before modifying production code, owner review should settle: (1) MCExpr canonicalization and symbol difference rules; (2) fragment-owned fixup offsets + site-state lifetime; (3) ELF section group identity and sh_link/info contract; (4) CFI fail-closed behavior versus Linux source acceptance; (5) which Target ABI/ISA capabilities are real and which are intentionally unsupported.

The next implementation proposal should include a **whole-repository change map** with old-callers -> replacement, risk per Target, deletion of superseded paths and tests in one batch. No scattered ISA-specific additions to Common.

## Reference implementations (architectural reference, not a compatibility waiver)

- LLVM MC design: https://llvm.org/docs/CodeGenerator.html
- LLVM MCAssembler current implementation: https://github.com/llvm/llvm-project/blob/main/llvm/lib/MC/MCAssembler.cpp
- GNU GAS internals (fragments, md_relax_frag and machine hooks): https://gnu.googlesource.com/binutils-gdb/+/cbe950e9fe8a56f788bcf985cc51733f18abb45b/gas/doc/internals.texi
