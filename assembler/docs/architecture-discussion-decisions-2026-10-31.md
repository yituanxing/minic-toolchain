# MiniAS architecture discussion: decisions and source-backed follow-ups (2026-10-31)

Status: **discussion record and implementation reference, not a claim of implementation or CI acceptance**.
Scope: MiniAS input/configuration, RISC-V ISA/ABI, source parsing, typed MC representation, and future object formats.
Development line: `refactor/minias-target-isa-abi-core` (Draft PR #667); retain **one MiniAS development branch**.

This document captures the design decisions reached during the 2026-10-31
discussion. It complements `architecture-rfc-mc-core-2026-10-28.md` (overall
architecture) and `architecture-convergence-2026-10-31.md` (implementation and
deletion ledger). A proposal here does **not** override the behavior of the
current tree or certify previously failed regressions.

## 1. Agreed principles (stable decisions)

**D01 — Build the complete *skeleton* with a deliberately limited first
implementation.** Model configuration, source semantics, target-specific
instruction interpretation, typed MC data, layout, fixups and object output
from the beginning. Initially implementing only a subset (for example,
little-endian ELF and RISC-V I) must not make that subset an invariant of
Common algorithms. Unsupported requested modes must fail explicitly, not
silently produce a misleading `.o`.

**D02 — Distinguish three ISA questions.**

| Question | Example | Owner |
| --- | --- | --- |
| What may execute on the target? (`target_isa`) | `rv64im_zicsr_zifencei` | MiniAS instruction legality and object ISA metadata |
| Which legal instructions will the compiler *choose*? (`compiler_emit_isa`) | `rv64i` | MiniC instruction selector; intentionally narrower |
| Which additional verification do we request? (`audit_policy`) | strict-I executable-byte audit | Testing/explicit assembler audit mode |

MiniC may generate only I-family instructions while preserving handwritten
inline assembly containing M/Z instructions. Such assembly is allowed **only
when the actual target ISA permits it**, including any source-local ISA
state. Thus a broad target `-march` plus a narrower MiniC code-generation
policy is different from passing `-march=rv64i` to the entire toolchain and
then expecting `mul` to assemble. Do not make MiniC's code-generation subset
a MiniAS restriction.

**D03 — Keep ABI independent from the compiler's chosen ISA subset.** MiniC
implements the calling convention; MiniAS does not reconstruct it by
examining instructions. MiniAS does validate compatible target settings and
record appropriate object metadata. On RISC-V, ISA architecture attributes
and ELF float-ABI `e_flags` are different outputs. An `.s` can carry
`.attribute arch`/`.option` information, but cannot in general tell MiniAS
which calling convention (`lp64` versus `lp64d`, for example) was intended.
The driver, source directives and target defaults are distinct configuration
sources; **conflict/precedence rules remain to be specified**, not guessed.

**D04 — Separate correctness-critical *configuration* from optimization.**
For the first stage, focus the user-facing correctness contract on `-march`
and `-mabi`. The following remain first-class design dimensions even if the
current implementation accepts only a subset:

| Dimension | Initial simplified support | Future capability that the skeleton must allow |
| --- | --- | --- |
| Endianness | little-endian | target-selected big-endian and correct byte output |
| Object format | ELF (`ET_REL`, correct ELF32/64 profile) | COFF and Mach-O through distinct writers |
| ISA specification | one explicit, reproducible compatibility baseline | versioned ISA/extension interpretation (`-misa-spec`) |
| Privileged specification | one target/toolchain-matched baseline | version-aware CSR legality (`-mpriv-spec`) |
| PIC | supported current GNU-compatible subset; non-PIC baseline | full PIC/pseudo and relocation semantics |
| Linker relaxation | only modes whose relocations are implemented; fail closed otherwise | complete `.option relax`/`R_RISCV_RELAX`/alignment semantics |
| Object attributes | correctly emit supported target/ABI attributes | richer formats and attributes |

The entries in the middle column are **temporary implementation scope, not
permanent defaults or a claim that every mode is already implemented**.
In particular, GNU GAS normally enables RISC-V linker relaxation by default;
if MiniAS chooses a restricted initial mode, it must state the difference.
`-mtune` and CPU scheduling/cost models belong primarily to compiler
optimization and are **out of scope for this correctness pass**.

**D05 — Configuration and assembler state are different objects.** An
immutable target configuration describes the selected ISA, ABI, byte order,
object format and target policy. A mutable source-local assembler state
supports `.option push/pop`, `.option arch,+ext`, PIC, RVC, relaxation and
other target dialects. Every retained instruction/fragment that needs it
captures an **immutable effective state at its source site**; layout and
encoding cannot consult an unrelated final file-wide state.

**D06 — Parse the assembly stream, not a compiler-style AST of the entire
`.s`.** The parser handles physical source, macro/conditional expansion,
labels, common directives and target statements in sequence. Persistent
*expression trees* and typed `MCInst`/`MCExpr`/Fragments are appropriate.
This follows the practical separation in GNU GAS `read.c`/`md_assemble` and
LLVM MC `AsmParser`/`MCTargetAsmParser`/`MCStreamer`. Streaming input does
not eliminate later fixed-point layout, relaxation or fixup resolution.

**D07 — Keep one owner for every semantic algorithm/object.** Common owns
source expansion, symbol/section identities, fragment storage and layout;
the Target owns ISA parsing, register/operand interpretation, feature checks,
encoding choices and target fixups; the object model/writer owns serialized
format. Do not add an ISA-specific branch to Common to make one Linux source
pass. A target may propose encoded bytes/fixups, not independently mutate
global Section/Symbol/ELF storage. The same principle applies across all
four backend families; RV32/RV64 remain one RISC-V Target.

**D08 — Improve implementation quality independently from architecture.**
Use RISC-V I as a first full vertical slice to test the *whole* MiniAS path.
Measure MiniC's I-generation quality and MiniAS's I-assembly quality as
separate acceptance gates. Then add M/A/C/Z instruction support by extending
Target descriptions, semantics and tests rather than inventing new public
parsers, layout engines or writers. C/relaxable encodings must also test
whether the skeleton genuinely supports variable lengths.
This is an **architectural acceptance slice**, not permission to remove or
downgrade existing M/Z support, any of the four backend families or their
GNU/Linux regression requirements.

**D09 — Refactoring must eliminate proven duplicate owners, not merely move
source lines.** Preserve valid Target-specific complexity (x86 ModR/M,
AArch64 memory and mapping symbols, RISC-V RVC, LoongArch relocations).
Do not delete a second table/parser just because its syntax resembles
another: prove identical responsibility and verify with positive/negative
GNU comparisons. After migrating consumers, remove obsolete code and
compatibility branches in the same coherent batch where safe.

**D10 — Discussion, implementation and acceptance evidence are separate.**
No architectural proposal implies code has changed. Local A0/GNU byte
comparisons, GNU link, Linux Image and QEMU P0/P1 are distinct gates, always
at the **exact commit tested**. For changes, preserve the original GNU
physical `.s`, assembler-only MiniAS provenance and GNU downstream tools;
avoid a full Linux rebuild when a focused object/link/runtime gate answers
the question. Report production/test/docs/CI added/deleted/net lines per
commit. Do not silently downgrade the acceptance matrix.

**D11 — Let real programs determine the implementation order.** This is a
two-person engineering project, not a promise to implement every GAS/LLVM
syntax, ISA-spec revision or obscure object attribute before MiniAS is useful.
Keep the skeleton and correctness contracts sound, accept explicitly scoped
feature gaps, and close gaps when real original assembly exercises them.
Avoid spending an iteration on a standards matrix, performance tuning, or
premature future-format abstraction while a current source/fixup/ELF owner
boundary remains unsettled. A successful source sample proves only that
sample; unimplemented legal input still exists. This is a staged *product
strategy*, not permission to misassemble accepted input.

**D12 — Batch integration and keep persistent evidence.** Accumulate related
verified source cleanup, focused tests and architecture notes into a coherent
MiniAS-branch commit/push. Do **not** create a remote commit for each tiny
helper deletion. Equally, do **not** leave all verified work solely in an
automatically cleaned temporary workspace. Before pushing, inspect the
upstream branch, use a clean worktree and record the exact validation scope.

## 2. Agreed data flow and ownership boundary

```text
Invocation / Target Configuration (ISA, ABI, format, endian)
  -> SourceGraph (physical input, stable source-site identity)
  -> Source Expansion (macros, repeats, conditional assembly)
  -> Common Statement Parser (labels, common directives, expressions)
  -> Target Parser (instruction/operands, features, target directives)
  -> typed MCInst / MCExpr / semantic Fragments + site state
  -> Common MCLayout, Target relaxation and fixup proposals
  -> Target encoding -> Common fragment commit
  -> semantic object model -> ELF writer (future COFF/Mach-O writers)
```

Do not read this as an instruction to add a new file or abstraction for every
line. The boundaries concern **data ownership and permitted operations**.
`MCInst` is a machine-instruction semantic object, not another compiler IR.
Pseudoinstructions are lowered within the owning Target; machine-independent
directives are processed by Common. Raw instruction data must retain a typed
semantic distinction from ordinary DATA when the target object format needs
it (for example AArch64 `$x`/`$d` mapping symbols).

## 3. Verified current implementation: keep versus repair

The following was checked against the active MiniAS source tree on
2026-10-31. Locations are pointers for later investigation, **not** proof
that a design change has been made.

### Already valuable and should be retained

- `source_graph.c` owns physical input and stable `SourceSiteID`;
  `source_expansion.c` manages macros/repeats/conditions. Source replay is a
  conservative correctness fallback for layout-sensitive conditionals.
- `assembler.c:add_stmt()` records `MiniAsStmt` and its source-site snapshot,
  then invokes `TargetOps.parse_mcinst`. RV/AArch64/x86/LoongArch measuring
  and encoding generally consume the retained typed `MiniAsMCInst` rather
  than reparsing source operands in a second encoder.
- `mc_operand_semantics.c:minias_mcinst_alloc/destroy` gives the instruction
  one lifetime owner; `capacity` permits safe disposal of partially parsed
  operands, and Target-private memory/payload has explicit destructors.
- `expression.c`/`mc_expr.c` have a shared expression grammar and distinct
  evaluation policies (wrapping immediates, checked absolute and deferred
  symbol/layout expressions). Expression ASTs do **not** imply a whole-file
  assembly AST.
- `mc_lexer.c` supplies shared balanced operand splitting. `MiniAsStmt`
  typed Fragment kinds, `MCLayout`, encode-result/commit and fixup ownership
  should remain one Common path.
- RISC-V fixed-form `rv_mc_table` and dynamic `rv_parse_forms` cover
  different families; currently this is **not evidence of redundant tables**.
  `rv_scalar_machine_word` and branch planning are shared within the
  RISC-V Target where genuinely applicable.

### Proven gaps or structural risks (not all current runtime failures)

| ID | Priority | Evidence and observed boundary | Required direction |
| --- | --- | --- | --- |
| P01 | P0 | `riscv_profile.c:parse_isa` sets `strict_i_xlen` automatically for exact RV32I/RV64I; `riscv_target.c:strict_base_i_check_sections` and `riscv_validate_common_directive` then reject executable raw data/`.org` as an extra audit | Separate ordinary `target_isa` legality from explicit `strict_base_i_audit`; keep the valuable audit but do not redefine GAS-compatible `-march` |
| P02 | P0/P1 | `riscv_target.c:riscv_target_directive` currently rejects `.option arch,+ext` under explicit `-march`; legacy +v/+zbb handling is a no-op. `MiniAsRiscvState.option_stack` saves only PIC/RVC/RELAX bits | Add scoped effective ISA state and save/restore it; capture the correct site ISA for MCInst, layout and encoder without accidentally changing file-level attributes |
| P03 | P1 | `riscv_profile.h` uses one 64-bit feature mask; `riscv_profile.c` recognizes a limited fixed-version extension table and lacks general version-string parsing | Define an extendable versioned ISA/extension set and dependency validation before many Z extensions are added. No need to implement all extensions immediately |
| P04 | P1 | All four parser callbacks now use opaque Common-owned `MiniAsParseContext` services (§20); measurement uses read-only views (§18); parse-site snapshots/alignment read Target state only (§19); post-emit and pre-layout are narrowed (§15–16). Encoding/relaxation still receive mutable `MiniAs*`. | Read the real encode/relax Common transaction APIs before any future phase migration; do not duplicate the encoder or hide necessary writes |
| P05 | P1/P2 | `assembler.c:minias_statement_process` still reads/writes `SourceContext` internals and owns considerable conditional/directive routing; source expansion calls back into it | Complete Source ownership and typed statement routing; a smaller file alone is **not** proof of a clean boundary |
| P06 | P1 | `aarch64_target.c:a64_record_source_mappings`/`validate` check `strcmp(stmt->op, ".inst")` and alignment directive spellings **after** parsing to determine object mapping semantics | Store parse-site typed instruction-data/alignment provenance; keep original text only where genuinely needed for diagnostics or initial expression compilation |
| P07 | P1 (future) | RV RVC measurement/encoding reads `state->isa_features` globally while `stmt->site.mode_bits` records only selected option bits | When scoped `.option arch` is implemented, bind the effective ISA/encoding policy to each fragment to avoid consulting the wrong state after `pop` |
| P08 | P1 (future) | `MiniAsAssemblyConfig` currently contains ELF-class/ELF-flag/machine fields and no general object-format/endianness model | Preserve ELF implementation, but define the configuration/object-model seam before adding COFF/Mach-O or big-endian output; do not bake ELF headers into MC semantics |

`MiniAsStmt.op/args` should **not** be removed wholesale. They serve useful
parse-time and diagnostic purposes. The goal is to remove *late semantic
re-decoding from source spelling*, not to ban source text from the model.

## 4. Proposed future work order (not yet approved implementation changes)

1. Freeze the distinct concepts `target_isa`, `compiler_emit_isa`, and
   explicit audit policy. Validate ISA/ABI/source attributes and conflict
   rules before extending the command-line surface.
2. Specify immutable TargetSpec, mutable Assembler/TargetState and immutable
   effective site snapshot. First exercise RV I plus a scoped M/Z probe;
   explicitly unsupported extensions must error rather than be ignored.
3. Keep current `MiniAsStmt -> MCInst` parse-once chain; move AArch64
   late mapping classification into typed parse-site semantics and close
   SourceContext's private data boundary.
4. Introduce phase-scoped Target views/emit sink incrementally, preserving
   retained layout, fragment commit, fixup identity and existing backends.
5. Use RV I for the complete differential vertical slice, then extend
   M/A/C/Z through Target-owned forms and versioned capability checks.
6. Add object writers/formats and endianness policies only when backed by
   real tests; retain format-neutral identities and semantic relocations.

For each implementation batch, list exact code paths to **delete**, their
callers and compatibility bridges; verify the same-input GNU oracle and
affected Linux object/link/runtime slices before any broad remote CI.

## 5. Source and reference map

MiniAS current sources:

- `assembler/src/{invocation.c,riscv_profile.c,riscv_profile.h,riscv_target.c,riscv_target.h}`
- `assembler/src/{source_graph.c,source_expansion.c,assembler.c,mc_lexer.c}`
- `assembler/src/{minias_internal.h,mc_operand_semantics.c,expression.c,mc_expr.c,mc_layout.c}`
- `assembler/src/{riscv_encode.c,aarch64_target.c,x86_64_target.c,loongarch64_target.c}`
- `assembler/src/elf_writer.c`, `elf/include/minielf.h`

Upstream primary references:

- GNU GAS RISC-V options: <https://sourceware.org/binutils/docs/as/RISC_002dV_002dOptions.html>
- GNU GAS RISC-V directives: <https://sourceware.org/binutils/docs/as/RISC_002dV_002dDirectives.html>
- GNU GAS RISC-V attributes: <https://sourceware.org/binutils/docs/as/RISC_002dV_002dATTRIBUTE.html>
- GCC RISC-V options: <https://gcc.gnu.org/onlinedocs/gcc/RISC-V-Options.html>
- LLVM MC/MCStreamer overview: <https://llvm.org/docs/CodeGenerator.html#the-mcstreamer-api>
- LLVM AsmParser: <https://llvm.org/doxygen/AsmParser_8cpp_source.html>
- LLVM RISCVAsmParser: <https://llvm.org/doxygen/RISCVAsmParser_8cpp_source.html>
- GNU GAS common parser: <https://gnu.googlesource.com/binutils-gdb/+/950386c651f7a852fe7e1215d7481caf7f2c91d0/gas/read.c>
- GNU GAS RV target parser: <https://gnu.googlesource.com/binutils-gdb/+/cf525621820060a5c77efa153e6d2ec6a5ad47c9/gas/config/tc-riscv.c>

## 6. Cross-subsystem ownership notes from the continued source walk

These observations extend section 3's short priority list. They are a
backlog of *boundaries to revisit when a feature touches them*, not a
request to implement all of them next.

### Sections, subsections and symbols

- Common SectionID identifies a section by its full identity (name, group
  signature, link-order anchor and optional `unique` key), not merely its
  textual name. The subsection is separately keyed by `(SectionID, number)`;
  final concatenation follows subsection order. Keep this independent of
  the eventual serialized section header format.
- Common SymbolID and SourceSiteID survive parse and layout; the name
  interner's NameID is distinct from a defined symbol's final identity.
  The semantic `.set`/`.equ` graph, symbol visibility/type and ELF symbol
  ordering have different responsibilities. Do not collapse them into a
  single string-table lookup.
- Current symbol type/binding/visibility representations and many Common
  flags are ELF-flavored. This is reasonable for the first ELF vertical
  slice; choose format-neutral attributes when a second real writer demands
  them, while preserving stable semantic SectionID/SymbolID now.

### Fixups, MCData and post-layout writers

- A Target instruction proposes typed fixups inside `MCEncodeResult`; the
  Common fragment transaction commits them. Fixed-width MCData also stages
  through this transaction. Source `.reloc` owns its *explicit section*
  fixups, while CFI and DWARF serializers own genuinely *synthetic* fixups.
  These are three distinct entry paths into one Common Fixup store.
- An ordinary local/same-section reference may be resolved by Target or
  Common layout, while retained link relocations are mapped by the ELF
  writer (including target-specific local-symbol/section-symbol policies).
  A generic new late-resolution engine is not justified until a concrete
  cross-format or correctness failure calls for it.
- `.eh_frame`/`.debug_frame` CFI and `.debug_line` are legitimate distinct
  post-layout producers with different serialized formats. They share
  anchored labels, final layout and Fixup ownership; they are **not**
  redundant writer algorithms to merge merely because both use DWARF.
  Tiny separate ULEB appenders are likewise not sufficient evidence for a
  large new abstraction.
- A same-name source-created `.debug_frame` or `.eh_frame` section can be
  encountered by synthetic CFI append. `.debug_line` currently handles a
  collision differently. First establish the actual GNU mixed-source
  behavior; do not call the difference a bug without an original `.s`
  oracle.

### Source, Target and object format boundaries

- Source expansion legitimately replays layout-dependent `.if` decisions;
  keep stable source-site identities and avoid a second parser. Historical
  source text may remain for diagnostics; the important ban is *late
  instruction-semantic re-decoding* of those strings.
- `MiniAsTargetOps` still hands a broadly mutable `MiniAs *` to several
  phases. The existing single fragment commit mitigates this, but future
  parse/layout/encode views should expose only the capability each phase
  needs. Extract this incrementally, with an owner/caller deletion proof.
- AArch64 object mapping (`$x`/`$d`) and alignment semantics currently
  inspect `.inst`/alignment directive spellings after parsing. Preserve the
  existing final mapping sort, but migrate to typed source-site provenance
  when that Target interface is next changed.
- ELF32/64 little-endian remains the *implemented* output family. Windows
  COFF and macOS Mach-O are useful future **minimal runnable** end-to-end
  validation targets for the four-backend skeleton; this discussion does
  not authorize or schedule their implementation yet. The immediate task
  remains reading, ownership cleanup and existing ELF correctness.

## 7. Open correctness probes, carefully scoped

| ID | Concrete observation | Treatment |
| --- | --- | --- |
| P09 | `.reloc` accepts less GNU grammar than full GAS | Document a syntax gap; extend only against a real source/oracle |
| P10 | Target can pre-resolve some locals; ELF writer handles retained relocs | Preserve the existing division until a failing example proves another resolver necessary |
| P11 | RV same-section `GLOBAL`/`WEAK` branch/jump relocation selection and fixup elimination differed from GNU | **Closed in §21:** one branch/layout plan preserves GNU relocs and link-time override; six RV32/RV64 GNU byte/RELA/LD oracles |
| P12 | Common binding/type/visibility fields currently resemble ELF enums | Keep now; revise at the real second-object-format boundary |
| P13 | At the `fed9209` baseline, fixed MCData Fixup wrappers had a direct-record fallback even though their only callers emit active Fragments | **Cleaned in section 8's batch**: always stage through `MCEncodeResult`; explicit `.reloc`, CFI/DWARF and late ULEB paths remain independent |
| P14 | RV parse consults `as->elf_flags & 8` for an RVE feature; AArch64 encode checks interned `"."` NameId | Source/target ownership debt, not a reason to rewrite all ISA parsing now |
| P15 | AArch64/RV `validate_output` may inject mapping symbols or adjust alignment | Legitimate late output effects; eventually name this mutable finalization capability honestly and scope it |
| P16 | CFI source/synthetic section collisions differ from the `.debug_line` collision guard | Defer judgment until a GNU mixed-section byte/relocation oracle exists |

In particular, P01 (automatic strict-I executable-byte audit) is not the
same as *instruction-set legality*. P02/P07 (scoped `.option arch` effective
ISA) are coherent future Target-state work. Full ISA extension versions
(P03), future COFF/Mach-O and big-endian modes must not block present ELF
maintenance. None of these deferred points is declared solved here.

## 8. Small proven owner cleanup (focused, no extra architecture)

The two MCData Fixup wrappers `minias_add_fixup` and
`minias_add_fixup_symbol_id` are only called by `mc_expr.c`. The containing
`minias_emit_mc_data` requires an active retained fragment and
`MCEncodeResult`; the old direct `minias_record_fixup` fallback therefore
cannot be a valid production caller. The wrappers can delegate directly to
`minias_stage_common_fixup`, leaving the real Common fragment commit as
the sole recording point. This intentionally does **not** remove the
section-fixup API needed by `.reloc` and synthetic sections, or the explicit
late-ULEB pairing helper.

Common `minias_grow_array` also supersedes two small local dynamic-array
allocation algorithms (`cfi_grow` and `debug_grow`) across two CFI and three
debug-line call sites. Their initial capacities differ (16 versus Common's
8) but this is not a semantic contract. Preserve debug-file new-slot
zeroing and the actual CFI/DWARF parse, layout and serialization behavior.
Update CFI's obsolete header comment to describe already supported
`.eh_frame` instead of claiming it is unimplemented.

For this cleanup, the narrow regression set covers fragment-transaction
ownership, ordered/explicit/synthetic Fixups, CFI `.eh_frame` and dual
frame output, signed CFI offsets, `.file`/`.loc`/DWARF views, ELF32/64
DWARF relocations and mixed-section x86 GNU comparison. Exact results must
be recorded for the resulting commit, not inherited from another worktree.

The local pre-commit verification on upstream baseline `fed9209` plus this
four-file diff: Release MiniAS build **PASS**; 14 focused Fragment/Fixup,
CFI and DWARF/GNU scripts **14/14 PASS**; `check-minias-a0` **PASS,
objects=30**, using a temporary `pyelftools` test dependency; `git diff
--check` **PASS**. This establishes assembler-local coverage, not new
Linux Image/boot, remote CI or full multi-architecture GNU cross-oracles.

## 9. Deliberate limits and next review direction

Next continue the **Common semantic owner → retained MC Fragment → final
Fixup → ELF Writer** boundary, crossing into an individual Target only
where the source path demands it. Prefer an entire subsystem and its caller
graph over micro-fixes, but delete only branches shown unreachable by code
and verified probes. Maintain a single development branch and batch pushes;
do not schedule COFF/Mach-O, full spec-version matrices, or tuning now.

## 10. Next owner batch: one Fixup request staging vector

The next read followed all three active `MCEncodeResult` Fixup producers:

1. Target-owned instruction symbol references (`minias_encode_add_fixup_ref`);
2. Target-owned absolute-zero linker-relax markers
   (`minias_encode_add_fixup_abs0`, currently reached from LoongArch);
3. Common fixed-width MCData symbol references (`minias_add_fixup*`).

Previously each independently implemented the same `result->fixups`
capacity increase, record allocation/zeroing, and append. Two instruction
variants also repeated the same fragment section/offset/bounds validation.
The unified `minias_encode_result_stage_fixup` in `mc_encode_result.c` now
owns request allocation and append. One Target-private helper verifies the
common instruction fragment contract while preserving **distinct** legality
checks and diagnostics: named SymbolRef widths 2/4/8, absolute-zero width 4,
and MCData's 1/2/3/4/8 or zero-width ULEB kinds. The source semantic
differences therefore remain explicit; no Target has gained direct access
to the final global Fixup table.

The same read confirmed that the ELF writer plans local-symbol liveness,
folds eligible relocations to section symbols using a Target policy, checks
final fragment bounds, then delegates layout/serialization to `minielf`.
The apparently similar CFI/DWARF synthetic writers, explicit `.reloc` and
Target instructions are **not** duplicate owners to delete. AArch64
`validate_output` still uses late source directive spellings for mapping;
RISC-V adds mapping symbols in finalization; both remain explicitly tracked
future interface debts, not silently solved by this batch.

Local pre-commit coverage for this second Fixup staging batch: Release build
PASS; the actual 14 existing Fragment/Fixup/DWARF/GNU scripts PASS (a test
runner initially typed one wrong script filename and then ran the correct
`probe-mcexpr-affine-ast-gnu.sh` successfully); LoongArch GNU pseudo/RELAX
oracle PASS with four RELAX records; A0 **30/30 PASS**. This is not a full
Linux Image + QEMU P0/P1 result.

The earlier `8ca9421` full Linux four-architecture workflow was **FAIL**
after AArch64's large `.if`/shared-expression-AST blocker. Later
`fed9209` repairs large GNU macro expressions, and exact-HEAD contracts,
GCC single-AS runtime/BusyBox and focused oracles pass, but the automatic
full Linux workflow on `fed9209`/`dfe1bae` was **SKIPPED**, not a new full
PASS. The next coherent implementation push may carry the explicit
`[minias-linux-four]` CI opt-in marker to close this background acceptance
debt. Keep the test SHA and opt-in outcome separate from these local claims.

## 11. ISA legality versus the optional encoded-byte audit (agreed follow-up)

Do **not** use `-march=rv32i/rv64i` as a hidden request to certify that
every byte in an executable section decodes to Base-I. `-march` selects the
Target's typed instruction/extension legality and ELF ISA attribute. GNU GAS
accepts `.word`, `.byte`, `.org`, and supported raw `.insn` forms under exact
I, including words that happen to encode an M/F instruction. Source-directed
data is not a typed `mul` or `fadd.s` mnemonic. An implicit strict scan would
reject legitimate GCC/GNU input, conflate two separate policies, and make
the assembler harder to use for real projects.

Preserve the stricter *optional* audit through the existing legacy C entry
point `minias_assemble_file_target_isa(..., strict_i_xlen)`. It rejects raw
executable fragments and validates final machine words independently of
typed ISA checks; do not add another CLI flag for this private/test use case.
The normal invocation's `strict_i_xlen` remains zero even for exact I, while
typed feature gates must run without depending on that audit state. Preserve
the current `.option arch` fail-closed limitation until the scoped mutable
ISA work (P02/P07); it is not solved by relaxing raw-data policy.

An A0 follow-up caught `pause` mislabeled as an always-legal privileged
instruction. The typed form now requests `Zihintpause` explicitly; GNU GAS
rejects it under `rv64i` and accepts `rv64i_zihintpause`. `sfence.vma` is
different: it remains available under `rv64i` as a privileged instruction,
matching GNU. The temporary extension inventory records the ISA attribute
for `zihintpause` without attempting a full standards/version expansion.

**Local verification on this pending batch:** Release MiniAS build PASS;
`check-minias-a0` **PASS, objects=30** (using the existing temporary
`pyelftools` test dependency); six separate RISC-V probes **6/6 PASS**:
RV32 width, option RVC, target-profile single source, real GCC `-S`
RV64I/inline asm, executable raw directives, and typed FP. A new direct
public C API probe passes both RV32I and RV64I with **8 forbidden escapes or
extension words rejected** and **2 typed-I examples accepted**. The
ordinary raw-data probe covers 22 aliases × 4 profile cases = 88 successful
assemblies. The installed GNU AS does not recognize four source/profile
combinations among those aliases; they are MiniAS-only acceptance checks,
not claimed GNU byte parity. GNU also pads some final `.text` raw data to
a multiple of four bytes while MiniAS preserves its input length: the probe
compares the payload byte-for-byte and bounds this known trailing padding
gap to at most three bytes. GNU numeric-only `.insn` spelling is a separate
syntax gap; the implemented typed R/I `.insn` forms have GNU byte oracles.

These results are **assembler-local**, not evidence that the changed batch
passed Linux/QEMU. The separate full Linux workflow on earlier `ae70920`
reported **all three** x86-64, AArch64 and LoongArch64 MiniAS Kbuild/QEMU
P0/P1 steps successful, with GNU AArch64/LoongArch64 baselines successful.
The AArch64 job was still preserving artifacts and had not completed its
final result at the last check. Keep the exact-head distinction in future
acceptance reports; this remains the earlier commit, not this batch.

## 12. Object finalization phase and last Common growth duplicates

The source-backed P15 audit found that `validate_output` was not validation
only: AArch64 injected `$x`/`$d` mapping symbols and adjusted Section
alignment from retained `.inst`/alignment sites; RISC-V checked optional
Base-I bytes *and then* inserted `$x<isa>` symbols. Those effects are legal
late object construction, not dead logic. `MiniAsTargetOps` now separates
`validate_output` from an optional mutable `finalize_output` callback. The
driver runs them in this order after Common emission and before the ELF
writer. AArch64's alignment validator and RISC-V's strict byte validator
stay in validation; mapping/section completion is explicit finalization.
LoongArch keeps validation only; x86-64 needs neither optional callback.

This is an honest phase boundary, **not** an assertion that Target callbacks
already have immutable phase-scoped view types. The AArch64 mapping planner
still consults a few source directive spellings. Both remain future
TargetParseView/TargetLayoutView and typed site-state cleanup, rather than
prematurely redesigning the mapping algorithm or implementing COFF/Mach-O.

The same source read identified four remaining generic reallocation loops:
materialized Section data, numeric-label encounter counts, numeric-label
text rewriting and string literal decoding. They now call the established
`minias_grow_array` capacity owner, preserving zero-initialization of newly
allocated label counters and the existing semantic NOBITS checks. The
rewritten-text request also checks `size + count + terminator` overflow
before allocation. The expression AST's custom `realloc` is retained:
its inline-array-to-heap migration and hard AST node cap are distinct
storage semantics, not another generic vector growth policy.

Local pre-commit results for this pending batch: Release MiniAS build PASS;
A0 **30/30 PASS**; strict GNU AArch64 mapping/section-alignment **3 PASS**;
RISC-V target attribute/order/CSR probe PASS; LoongArch GNU odd-byte
instruction-alignment probe PASS; x86-64 and RV64 SourceGraph replay/GNU
oracles **2/2 PASS**; Target/Common structural boundary PASS including
an explicit validation-before-finalization-before-writer assertion. Remote
Linux/QEMU on this still-unpushed tree has **not** run.

The preceding `d5fe7b3` batch's exact-HEAD ISA/ABI, x86/Loong GNU oracle,
four-backend compiler runtime, GCC single-AS A/B, BusyBox, and runtime
ladder workflows have all reported SUCCESS. Its full opt-in Linux runtime
job was SKIPPED. Do not transfer the earlier `ae70920` full Linux P0/P1
certificate to `d5fe7b3` or to this finalization cleanup.

## 13. Typed AArch64 parse-site provenance and RV E ABI ownership

After `e2698e7`, the post-emit phase is explicitly divided, but P06 still
had an avoidable late lexical dependency: `a64_record_source_mappings` and
`validate` checked retained source op strings to decide whether `.inst` was
machine code or whether `.align/.p2align/.balign` was an explicit alignment.
Those decisions belong to source interpretation, **before** MCLayout.

`capture_site_state` now receives the statement being captured, alongside
the Target's existing mode snapshot. AArch64 records two Target-private
`provenance_bits` at parse time: `.inst` data with instruction mapping and
explicit alignment. Finalization consumes only these retained semantic bits,
not the original directive spelling. The common `MiniAsTargetSiteState`
stores the opaque bits but never interprets AArch64 syntax; x86-64 and
RISC-V preserve their existing mode snapshots. This closes the proven P06
late-string use without adding an AST or second mapping engine.

The parallel P14 read found RISC-V's typed MCInst register gate reading
`as->elf_flags & 8` to infer RV32E, including `-mabi=ilp32e` without
`-march`. `riscv_init_target_state` now captures `rv32e_abi` from the
resolved Target configuration, and `riscv_encode.c` consults that policy
beside `RISCV_ISA_E`. The Common ELF output flags are no longer used as a
Target operand-legalization input. A0 now asserts positive x15 and rejected
x16 in the implicit-ISA ILP32E case, including the correct ELF RVE flag.

Local coverage on this pending batch: Release build PASS; A0 **30/30 PASS**;
GNU AArch64 strict `.inst` `$d/$x` mapping, `.data` alignment and six
source-mapping cases PASS; RV32 XLEN GNU probe PASS; SourceGraph x86/RV
exact GNU bytes/symbols/replay **2/2 PASS**; structural boundary check PASS,
including bans on late AArch64 statement-op comparisons and RISC-V MC's
read of Common `elf_flags`. A full exact-head Linux boot result is **not
claimed** here.

The preceding `e2698e7` exact-head CI subsequently completed SUCCESS for
the target ISA/ABI contract, x86/Loong GNU oracle, four-backend compiler
runtime, GCC single-AS A/B, real BusyBox, runtime ladder and Native Strict
Backend gates. The optional Linux kernel workflow on that commit was
SKIPPED; retain the result and SHA distinction.

**Still open:** P04 phase views should eventually prevent Target mutation
of Common storage *by type*, rather than relying solely on transactional
emission and reviewed callbacks. AArch64's mapping planner legitimately
traverses final Fragment and Section layout and must gain a read-only
FinalizeView when that interface is built. LoongArch's complete-section
branch-relaxability cache likewise needs a query view, not a handwaved
deletion of its global lookahead algorithm.

## 14. Common Fixup creation and Target finalization write capability

The previous batch at `c325a07` already owns AArch64 `.inst`/alignment
provenance in Target-private parse-site bits, so this batch reuses that
contract without introducing a duplicate Common intent model. Two other
mutable-ownership seams remained:

- Fixup creation was followed by three `as->fixups[fixup_count-1]` edits
  to install `prefer_section_symbol` in DWARF CIE references, late ULEB
  relocation pairs, and normal MC fragment commit. The binding preference
  now belongs to the same Common Fixup creation request. Explicit `.reloc`
  and unrelated CFI/DWARF requests pass false; the CIE base and late ULEB
  requests retain their established true policy. No caller depends on the
  last appended global Fixup slot to complete its transaction.
- AArch64's post-layout mapping planner legitimately raises a Section's
  input alignment for `.inst` in `.data` and for executable instruction
  transitions, but formerly wrote `section->align` directly. It now asks
  Common through `minias_require_target_section_alignment`, guarded to
  the short `finalize_output` invocation. The necessary `$x/$d` mapping
  Symbol requests still use the existing Common ownership API.

This is an incremental capability boundary, not a claim that
`MiniAsTargetOps` now exposes fully immutable phase-scoped views. Final
`TargetParseView`/`TargetLayoutView`/ObjectFinalizer view types, the
SourceGraph replay contract and ELF ObjectLowering remain separate work.

Local pre-push acceptance on the rebased `c325a07` tree: Release `-Werror`
and A0 **30/30 PASS**; strict AArch64 GNU `.inst` mapping and
alignment, including six mapping transition cases PASS; GNU `ld -r` CFI
`.debug_frame` ELF32/ELF64 and RV64 CFI reference PASS; RISC-V and
LoongArch64 linker-relaxable ULEB GNU relocation/byte comparisons
**2/2 PASS**; five architecture/profile GNU CFI extended probes **5/5
PASS**; Target/Common structural boundary PASS. Do not infer a fresh
Linux/QEMU Image/P0/P1 certificate from these focused checks.

## 15. P04 first phase-type boundary: read-only post-emission layout

The first P04 slice is the final object phase, where there are only three
real consumers of completed layout: AArch64 validates instruction placement
and inserts `$x/$d` mappings/adjusts section alignment; RISC-V audits
optional strict Base-I bytes and inserts `$x<isa>` mappings; LoongArch
validates each instruction width. x86-64 needs no post-emit callback.

Both `TargetOps.validate_output` and `finalize_output` now take a borrowed
`const MiniAsPostEmitView` with **read-only Fragment and Section slices**
and opaque Target state, rather than full writable `MiniAs*`. The separate,
incomplete `MiniAsPostEmitErrorSink` carries diagnostics. Only finalization
receives `MiniAsPostEmitObjectSink` (opaque outside `assembler.c`), with
explicit Common functions for collision-safe mapping symbols and promotion
of an existing Section's alignment. Validation cannot obtain that sink or
touch Common-owned Symbol/Fixup arrays. The views are built *after* Common
completes emission and remain valid through the ELF writer preparation;
mapping symbols do not reallocate Section/Fragment arrays.

The §14 `finalizing_target_output` capability guard stays in force across
this type migration: Common's wrapper sets/resets it around the typed
finalizer callback. The new alignment sink delegates to §14's existing
guarded `minias_require_target_section_alignment` instead of adding a
second section-alignment policy. Mapping insertion through the new sink
is also gated to this phase. The §14 Fixup-at-creation corrections and
CFI/DWARF callers are preserved unchanged.

This is **not** a second ELF writer or a reimplemented mapping pass. AArch64's
source-order/subsection reconciliation, retrospective `$d`, zero-width
alignment and `.inst` cases retain the same algorithm and site semantics.
RISC-V keeps its canonical name/section-ordinal policy. AArch64's separate
CFI data-mapping callback remains in the earlier CFI emission phase; its
Common symbol API is deliberately not conflated with post-emission hooks.
The Target objects can still inspect immutable MCInst payload references,
but no post-emit callback is passed a writable Common root by type.

Local verification for this batch: Release `-Werror` rebuild PASS; A0
**30/30 PASS**; GNU strict AArch64 `$d/$x`, data `.inst` section alignment
and six source-mapping scenarios PASS; GNU LoongArch odd-position instruction
and 1-byte Kbuild feature probe PASS; x86-64/RV64 SourceGraph byte/symbol
oracles and replay **2/2 PASS**; structural Target boundary PASS with new
post-emission read-only/sink guards. This is local evidence; remote CI for
the pending commit is not yet claimed.

The preceding `c325a07` exact-head mandatory GitHub CI has since completed
SUCCESS for the target ISA/ABI, x86/LoongArch GNU oracle, four-architecture
compiler runtime, native strict backend, real BusyBox, GNU GCC single-AS
A/B, runtime ladder and MiniObjcopy/MiniStrip integration. The opt-in Linux
kernel/3536 workflows were SKIPPED; no fresh full-Linux certification is
implied for `c325a07` or this change.

**Next P04 slice:** Build a narrow read-only layout query for the *existing*
LoongArch full-section linker-relaxability cache and later constrain the
parse/measure/encode callbacks incrementally. The full-section lookahead
is required for correctness; deleting it to reduce direct reads would
regress GNU branch relocations. Do not grow this into a COFF/Mach-O or
versioned-ISA project now.

## 16. P04 parsed-SourceGraph layout policy: LoongArch forward lookahead

The LoongArch branch encoder previously called
`la_section_is_linker_relaxable(as, section, ...)`, which lazily walked all
`as->stmts` to find PCALA/GOT pseudo-MCInsts, then cached one boolean per
Section. It was **not dead code**: a branch can precede a later PCALA/GOT
macro whose linker-time relaxation changes branch distances. This is a
whole-source property that a prefix parser cannot decide correctly.

Common now calls an optional `TargetOps.prepare_layout` exactly after the
complete SourceGraph is parsed, EOF literal pools and local commons are
flushed, and before MCLayout. Its borrowed `MiniAsPreLayoutView` exposes
read-only retained Fragments, Section count and a Target-owned mutable
state handle; no writable Common root, Symbols, Section bytes or Fixups.
LoongArch scans that view once and stores its existing per-Section
relaxability bitmap in `LaTargetState`. The encoder now reads **only this
cached Target answer** and rejects a missing cache; the old mutable-
`MiniAs` source-array scan and its duplicate lazy-allocation path are
removed. Source replay reconstructs the Target context and reruns the hook
on the new complete source graph. Other targets have a NULL hook.

An added GNU fixture tests an early local B16 branch, later `la.local`
PCALA in the same Section, and an independent Section without the macro.
GNU and MiniAS agree on the mandatory relaxable Section's B16 relocation
and exact encoded bytes. **Observed optional divergence:** GNU GAS 2.42
also leaves a local B16 relocation in the independent clean Section;
MiniAS has already resolved that same-section displacement. We record the
difference, but do not spread a file-global relaxation flag merely to copy
redundant GNU metadata: it does not change the clean Section's correct
branch destination or link-time mutability. The strict GNU oracle still
compares all of the existing GCC pseudo fixtures and the new relaxable
Section's correctness-bearing bytes and relocation. This is our agreed
real-project criterion: identify the difference, retain the necessary
semantic boundary, defer rare standards parity without blocking progress.

Local tests for the pending batch: Release `-Werror` rebuild PASS;
Target/Common structural gate PASS (new pre-layout view/ordering/cache
ownership assertions); A0 **30/30 PASS**; GNU LoongArch aliases, GCC
pseudos, forward section lookahead and ALTERNATIVE relocations PASS;
AArch64 dot-branch linker oracle PASS; x86/RV SourceGraph GNU byte/symbol
replay **2/2 PASS**. Its remote exact-SHA CI is not yet available.

The preceding `2d5290b` mandatory CI is now completely SUCCESS for Target
ISA/ABI, x86/LoongArch GNU oracles, four-architecture compiler runtime,
Native Strict, real BusyBox, GCC single-AS A/B and runtime ladder. Full
Linux kernel and 3536 workflows remain opt-in/SKIPPED; their earlier
successes must not be attributed to this newer commit.

**Next P04 boundary:** Parse/measure/encode still accept mutable `MiniAs*`
for symbol lookup, errors and emission transactions. Shrink one real
phase at a time, preserving the retained MCInst and Common-owned emission
sink. No new ISA parser, generic object-format abstraction or second
section-relaxability algorithm is needed yet.

## 17. One ABI-aware Fixup policy, including ELF32/i386 rejection

The preceding batches already added immutable `MiniAsPostEmitView` and
`MiniAsPreLayoutView` interfaces. While reconciling that work, an independent
Fixup audit exposed a **real ABI mismatch**. `TargetOps` offered generic
`supports_fixup`/`fixup_relocation_type` callbacks and optional
`_for_object` overrides. The ELF32/i386 override fell back to AMD64 support
when a kind or width was not specifically handled. That let `.quad external`
or an AMD64 GOTPCREL-family request pass semantic validation on i386, only
to become the invalid placeholder `R_386_NONE` in the final ELF object.
The named `.reloc` parser also used the AMD64 spelling list for i386, wrongly
rejecting valid `R_386_GOTPC` and accepting some foreign ABI spellings.

Each Target now has just one `supports_fixup(const MiniAs *,kind,width)`
and one `fixup_relocation_type(const MiniAs *,kind,width)`. The x86 Target
explicitly enumerates permitted i386 widths/kinds independently of its
AMD64 table; its named relocation parser selects `R_386_*` or `R_X86_64_*`
by the configured object ABI. Common MCExpr and ELF lowering always use
the same Target answer; there is no secondary fallback policy. AArch64,
RISC-V and LoongArch keep their existing semantic fixup tables under the
new ABI-aware signature without changing their supported encodings.

New GNU positive oracles compare actual `readelf -rW` relocation fields and
`objcopy` `.data` bytes for i386/AMD64. Four GNU-negative inputs now fail
under both GNU AS and MiniAS, with no emitted `.o`. We deliberately do not
claim the shared ELF semantic comparator is an i386 oracle: it has no ELF32
i386 model, while the tested AMD64 fixture has an unrelated preexisting
local Section-symbol mismatch. The focused checks isolate the relocation
ABI and byte payload instead of hiding that difference.

Local verification on the reconciled Target/Common tree: `-Werror` build
and A0 **30/30 PASS**,
the structural Fixup ABI gate, strict AArch64 mapping/ELF/QEMU, LoongArch
GNU relocation/alignment, x86 ELF32/`.code16`, x86/Loong GNU dual-target,
RISC-V and LoongArch linker-relaxable ULEB **2/2**, RISC-V final `.option`
states **4/4**, and five GNU CFI ABI profiles **5/5** all PASS.
Record remote CI separately at its exact new
commit SHA. No new kernel Image/BusyBox P0/P1 result is implied.
## 18. P04 instruction-width measurement: four Targets, one narrow contract

The instruction measurement hook previously received mutable `MiniAs*` in
all four Targets even though no measurement algorithm needed to update
Common-owned Sections, Symbols, Fixups or source replay state. Common now
constructs a stack-borrowed `MiniAsMeasureView` exposing only opaque
**const Target state** and an opaque `MiniAsMeasureErrorSink`. The sink
forwards failures to Common's existing first-error-wins diagnostic path.
`TargetOps.measure_mcinst` has **one replacement signature**; there is no
compatibility callback, redundant measurement pass or ISA switch in Core.

The four existing width rules survive unchanged:

- **AArch64:** an existing typed MCInst is 4 bytes; a missing MCInst still
  reports its source line through the sink.
- **LoongArch:** retained Target-owned multiword lowering plans determine
  their actual byte count. Missing/invalid plans still fail rather than
  guessing a 4-byte instruction.
- **RISC-V:** `rv_rvc_encode_plain` and `rv_rvc_branch_form` now accept
  `const MiniAsRiscvState *` explicitly; the same first helper remains the
  actual encoding path. Parse-site `.option` state and ISA C membership
  still determine whether a 16-bit form is legal. LI's retained multiword
  plan and the ordinary 4/8-byte forms remain the only other width cases.
- **x86:** legacy 16/32-bit prefixes have one pure, retained-MCInst-driven
  planner consumed by measurement, branch relaxation and final encode.
  Its former writable-Core error side effect is replaced by a diagnostic
  return, reported by the appropriate Common error path; the machine
  prefix/operand-size decisions are not duplicated.

The structural Target boundary probe checks all four callback signatures,
the Common-private diagnostic sink and reuse of the RV/x86 encoding
alternative. Release `-Werror`, A0 **30/30**, GNU-exact RVC options,
x86 Linux `.code16`/`.code16gcc` **38/38**, AArch64 `$x/$d` mapping and
LoongArch GCC pseudo/relaxation oracles passed locally. This proves the
focused interface migration and its exercised encodings, **not** full
ISA syntax coverage or complete downstream Linux boot parity.

**Next P04 slice:** inspect the parse-side construction and ownership of
typed MCInst and its diagnostics; only reduce `MiniAs*` in a coherent
phase-sized batch. Encoding continues to require real Common fixup,
symbol and fragment-transaction operations, so do not force that code
through a fake read-only view or start a second encoder.

## 19. P04 parse-site Target state, and the actual Common parse capability

The four-Target parse audit found a useful distinction. **Source-site
state capture** and **default executable alignment** are pure Target-private
questions; actual `parse_mcinst` is not. Before this batch,
`capture_site_state(const MiniAs *, ...)` let the RISC-V, x86 and AArch64
snapshot callbacks see the entire Common object even though each only
read `target_state` and the retained input statement. The RISC-V-only
`default_exec_alignment_for_site` likewise inspected only the RVC ISA and
`.option` state. Both callbacks now take `const void *target_state`, passed
directly by Common; the old full-`MiniAs` signatures are gone. LoongArch
has no source-site hook, which correctly remains NULL. No machine mode,
GNU 16-bit prefix, mapping symbol or section alignment policy was changed.

**Parser audit — services that genuinely mutate or query Common:**

| Target | Required Common parse service | Why it cannot simply be read-only |
| --- | --- | --- |
| RISC-V | NameId symbol binding, expression/ISA diagnostics | Symbol names are interned during typed MCInst construction; XLEN/E ABI validation consumes the retained operands |
| AArch64 | NameId binding, symbol difference and absolute expression evaluation, literal queue, diagnostics | `ldr =symbol` adds an entry to Common's Section/Subsection-scoped literal pool for later `.ltorg` flush |
| x86 | NameId/difference binding, current symbol query, absolute expression evaluation, diagnostics; read-only ELF class | Defined same-section symbol pairs can select an initial short immediate; ELF32/i386 operand legality differs from ELF64 |
| LoongArch | NameId binding and diagnostics | Retained pseudo lowering plans remain Target-owned, while SymbolNameId belongs to Common |

Common creates each Fragment and invokes **exactly one** Target parser in
`add_stmt`; no second generic parser is waiting for deletion. Audit of
RISC-V's fixed/dynamic form tables, AArch64 register-alias/literal handling
and x86 `lock`/`notrack` recursive alias path found distinct, necessary
responsibilities rather than proven obsolete implementations. Do **not**
delete these to manufacture a negative line count. A real next parse phase
boundary requires a Common-owned opaque service set for binding, lookup,
absolute evaluation, literal requests and first-error diagnostics, plus
immutable Target state/ABI. It should replace all four actual parser
callbacks and their dependent helpers together, with no dual parser track.

Local verification after this small but complete source-site boundary:
Release `-Werror`, A0 **30/30**, Target/Common structural interface guards,
GNU x86 `.code16` **38/38**, RV RVC and Target profile, strict AArch64
mapping and literal pool, and GNU LoongArch pseudo/relaxation oracles all
PASS. The targeted literal-pool and x86/RV tests are needed specifically
because those parse dependencies must survive a future larger migration.

## 20. P04 one Common-owned parse service context, four Target parsers

The §19 dependency audit is now implemented as one callback replacement,
not an independent parser layer. `add_stmt` still creates the one persistent
instruction Fragment and captures its immutable parse-site ISA state; it
now lends the single Target parser a **stack-scoped opaque
`MiniAsParseContext`**. The complete context definition (`MiniAs *as`) lives
only inside `assembler.c`, not in the shared header or any Target. There is
no API to recover the full mutable `MiniAs*` from the context.

The Common service methods retain their **original algorithms**:

- `minias_parse_bind_symbol_ref` and its difference variant delegate to
  the existing Common SymbolNameId interning/binding logic. No second name
  pool or alternative symbol identity is created.
- Absolute operand/data and symbolic-difference evaluation delegate to the
  existing `mc_expr.c` grammar over the current source location and symbol
  graph. The new `minias_parse_eval_*` names avoid C linkage collisions
  with the existing direct Common/CFI APIs.
- `minias_parse_queue_literal` delegates to the exact pre-existing,
  section/subsection-scoped Common literal pool. An AArch64 `ldr =symbol`
  still reserves a stable label and is later flushed at the original `.ltorg`
  stage; there is no Target-local copy of that pool.
- Diagnostics preserve Common's first-error-wins buffer. `is_elf32`,
  `has_error` and read-only `target_state` accessors replace ad hoc broad
  inspection. x86's early short-imm candidate checks use a Common-owned
  Boolean same-defined-section symbol query rather than pointers into the
  whole Symbol table.

RISC-V's fixed and six dynamic form parser callbacks, LoongArch's typed
lowering-plan construction, AArch64's parser/helper family and x86's memory,
symbol and recursive alias parsers now thread the same opaque context.
Those Target functions still own all MCInst allocation and failure cleanup.
Measurement, relaxation, encoding, CFI, Common data directives and final
ELF write **retain their existing separate interfaces**. In particular,
encode/relax are not inaccurately made read-only; they genuinely require
layout, symbol resolution and fragment-transaction capabilities.

Full-release `-Werror` and A0 **30/30 PASS** on the integrated tree. The
Target boundary structural gate reports all four parser callbacks on the
single opaque service signature. Focused GNU oracles also pass: **7/7 x86**
(including ELF32/i386 and `.code16` 38/38), **5/5 AArch64** (literal pool,
alias, mapping and real-link branches), **9/9 RISC-V/LoongArch** (RVC,
XLEN/profile, LI/FP, ELF fixups, LoongArch GCC pseudos and altinstructions),
and x86/RV SourceGraph replay **2/2**. This is a phase-ownership and
focused code-generation regression result, **not** a full Linux/QEMU
certification at this commit. Remote exact-head CI must be recorded
separately after the integrated commit runs.

The production-only diff for this entire four-Target parse-service batch is
**+519/−418 lines (net +101)**, largely the one-time conversion of parser
helper signatures and diagnostic/binding call sites. No second parser was
added. The old callback type and its broad helper signatures were removed.

**Next:** inspect existing `encode`/relax Common operations as they are,
identify any real duplicated transaction or mutable-state ownership, and
only then pick another coherent phase-sized replacement. Do not introduce
COFF/Mach-O now or spend a batch filling theoretical syntax profiles.

## 21. Encode/relax ownership audit and GNU link-visible branch/LDR binding

The encode/fixup review found that Common already has **one**
`MiniAsEncodeResult` staging and commit path. `minias_emit_sections` /
`minias_commit_encode_result` own Section bytes and Fixups; the Targets
produce ISA bytes and relocation requests. `mc_layout.c` validates the
Target's expansion plan without maintaining a second encoder. An audit of
all four Targets found no duplicated Common emit transaction whose removal
would improve correctness. Keep that existing boundary; P04 is still
**partial** because encode/relax have broad Common access. Do not add a
parallel encoder merely to replace an interface signature.

Two concrete **link-time binding** failures did justify changes:

- **RISC-V P11:** A same-section `GLOBAL` or `WEAK` branch/JAL previously
  dropped the GNU-visible relocation; a branch to an undefined or
  cross-section symbol was rejected because the B immediate cannot carry
  a link-time relocation. The one Target-private branch resolver now
  determines local range and necessary 4/8-byte layout, while final encoding
  preserves a B/JAL relocation for GNU link-visible symbols. For nonlocal
  conditional branches it emits inverted B + JAL with the JAL fixup at
  `branch+4`; an independent JAL retains a JAL fixup. The same plan governs
  both relaxation and emission, including RVC candidate promotion.
- **AArch64:** Direct `ldr xN, symbol` to a same-section
  GLOBAL/WEAK/HIDDEN symbol now keeps `R_AARCH64_LD_PREL_LO19` so GNU ld can
  override a weak definition. Defined same-section LOCAL references still
  fold, and `ldr =symbol` retains its Common-owned literal-pool identity
  and separate `R_AARCH64_ABS64` pool relocation.

The new fixed GNU oracles compare **actual section bytes, ELF relocation
offset/type/symbol/addend, and GNU-linked bytes** rather than treating a
successful assembler exit as parity. `probe-rv-branch-binding-gnu.sh`
tests `rv32i`, `rv32ic`, `rv64i`, `rv64ic`, and `rv64gc` (RVC on/off), with strong, weak, undefined,
cross-section, explicit addend and long-range targets, including a strong
link-time override. `probe-aarch64-ldr-binding-gnu.sh` covers four direct
LDR relocations, LOCAL folding, literal-pool retention and the same weak
override case. Both enter existing mandatory GNU-capable CI workflows,
without a new optional workflow. This is a focused relocation/link audit;
it is not a claim of complete GNU syntax coverage or Linux/QEMU boot.

Integrated local evidence: Release `-Werror` and A0 **30/30 PASS**, six RV
GNU byte/RELA/link/undefined-negative profiles (**20 relocations each**),
the AArch64 LDR GNU byte/relocation/link oracle, x86 Linux `.code16`
**38/38**, LoongArch GNU alias/privilege parity, and the four-Target
structural ownership guard all PASS. Production-only change: three files,
**+62/−54 lines (net +8)**, excluding five new tests/fixtures, docs and
two existing CI workflows. This is correctness repair, not a wholesale
encoder or object-format rewrite.

**Next:** return to the remaining real correctness gaps P09 (`.reloc`) and
P16 (CFI synthetic-section collisions) or the next narrowed source
ownership batch. Endian and COFF/Mach-O remain deferred by the two-person
project's deliberate workload budget.
