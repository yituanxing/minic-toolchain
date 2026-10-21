# Cross-project architecture audit — 2026-10-21

Baseline: `22c8bcb9efcc06ac43c48ee5ecef308f276ac37e`; its focused owner CI #39570773358 completed
SUCCESS. This audit inspected the full tracked repository tree (2,689 files),
the compilation pipeline and its Core/Frontend/Backend source owners, ABI
helpers, MinicAS, MiniLD, source tool driver, and CI/test layout. It is a
module-level and structural review, **not** a claim that every source line of
every tracked test or historical disabled workflow was manually read.

## Repository structure reviewed

- Compiler: `src/compiler`, `src/core`, `src/frontend`, `src/target`.
- Four targets: RV64, x86-64, AArch64, and LoongArch64 through
  TargetInfo + Backend Registry. Existing GNU-as/ld differential runner is
  distinct from the RV64-only top-level `minic` complete toolchain driver.
- Toolchain: MiniPP, MiniAS, MiniAR, MiniLD and shared ELF reader/writer.
- Verification: `tests/`, 26 active workflows and 321 archived/disabled
  YAML workflows, numerous real-project regression scripts.
- Code ownership: 15,335-line historical `core_lower.c` had already
  become about 13,677 lines after constant-fact and bit-field restructuring;
  GNU ASM dispatcher now has seven ordered shape owners; Core IR
  construct/verify/dump are separate translation units.

## Highest remaining architecture risks

1. AST→Core expression dispatcher: 3,661-line dispatch still mixes
   conditional CFG, sequencing, unary builtins, scalar comparisons and
   pointer arithmetic. This batch isolates five **semantic families** behind
   matched-result contracts, preserving every existing returned status and
   the original branch ordering. The family helpers remain in the same
   translation unit because their current CFG/memory primitives are private.
2. AArch64/LoongArch64 direct emitters: a ~2,462-line and a ~2,277-line
   function combine frame/ABI planning with ISA instruction selection and
   terminators. Untangling these needs real ABI+QEMU differential gates,
   not a mechanical shared `MachineOpcode` abstraction.
3. MiniLD core: `linker_core.inc` about 8,542 lines, included with
   preprocessor symbol renaming. The section-GC and output-ordering owner
   boundaries remain a separate architectural project.
4. Parser: `parser_statement.c` about 5,955 lines and
   `parser_global.c` about 5,829 lines; static/runtime aggregate
   initialization ownership deserves a dataflow review.
5. Mutable compilation state: reentry index and inline specialization
   caches are process/file scoped. Future compilation-context ownership
   must preserve current explicit begin/end and fail-closed invalidation.
6. Toolchain driver: `minic-cc --target` can select four ISA backends,
   while `tools/minic/main.c` intentionally rejects non-RV64 full
   preprocessor→assembler→linker pipelines. Do not conflate these two
   capabilities or silently reuse RV64 predefined macros.
7. Tests/CI: distinguish strict -Werror compile, focused QEMU, original
   all-config BusyBox cohort, and expensive Linux/RV64 2064-TU/performance
   certification. CI state must refer to the *exact commit*, not a previous
   successfully tested ancestor.

## Refactoring invariants and future direction

- Core owns source C semantics and explicitly target-parametrized data
  models, **not** target register allocation or ABI machine lowering.
- Backend controls its own ABI, frame strategy and target legalization.
- Never add Machine IR without an identified consumer; preserve optional
  `Core → target legalization → future MIR → emitter` extension.
- Failed/unimplemented lowering remains UNSUPPORTED/ERROR; do not silently
  default to zero/null or claim success.
- Rework complete responsibilities per batch, code review first,
  then strict compilation, GNU/QEMU real-project differential and
  architecture-specific regressions; do not use CI as a C syntax checker.

## This batch

Move five semantic families totaling 1741 original lines out
of `lower_expression` into private same-translation-unit helpers:
conditional evaluation and CFG joins; comma/short-circuit sequencing;
unary integer builtins; scalar relational comparison; and pointer
subtraction/offset. Each helper records whether an original return path
was reached, allowing exact original fallthrough for unclaimed patterns.
171 return sites were mechanically wrapped with a handled
marker, preserving all original status expressions and Core mutations.

Other architectural risks above were reviewed but deliberately untouched in
this batch; expanding the scope to separate ABI/linker/frontend rewrites
would mask regressions and violate per-owner validation.
