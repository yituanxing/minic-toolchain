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
| P04 | P1 | `minias_internal.h:MiniAsTargetOps` accepts mutable `MiniAs*` during parse, measurement, layout and encoding; this exposes sections/symbols/fixups beyond each phase's responsibility | Narrow to phase-appropriate Target parse/layout/encode views and a Common-owned emission sink, without breaking existing typed MCInst ownership |
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
