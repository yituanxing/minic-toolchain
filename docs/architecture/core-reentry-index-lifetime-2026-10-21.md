# Core incoming CFG index lifetime decision (2026-10-21)

## Whole-repository audit
Inspected all 188 production C/header/inc files in compiler, frontend,
Core, four target backends, MiniAS, MiniPP, MiniLD, ELF, archiver and
drivers. Reviewed the full tracked tree (2693 files), tests/workflow
organization, and current in-flight focused CI. This is comprehensive
production-source coverage and module-level audit of tests/historical
scripts, not a claim to have manually read all 2693 files line by line.

## Critical architecture owner
The incoming CFG reference index was a single process-global object in
`reentry_index.c`, implicitly scoped through begin/end calls surrounding
`minic_prepare_core_function_set`. This arrangement prevents safe nested or
concurrent translation-unit lowering, despite the backend registry and
TargetInfo already supporting all four architectures.

## Refactoring decision
- The function-set builder now owns an *opaque*, per-compilation
  `MinicCoreReentryIndex *` and tears it down before exiting.
- Each Core function lowering context borrows the index. The public
  standalone `minic_core_lower_function` retains its old no-index semantics.
- Build failures return NULL; all previously validated source CFG proof
  fallbacks remain conservative and unchanged. Incomplete graphs never
  publish their index's owning `program` pointer.
- Indexed query counters and perf output are associated with their own
  compilation and emitted on successful-index destruction.
- This does not yet remove the unrelated process-global inline-boolean
  specialization cache in `inline_boolean.c`; the next ownership stage
  should move that cache to its specialization pass and preserve the
  Linux first-500-TU cache performance evidence.

## Follow-up after a green full focused gate
AArch64/LoongArch64 giant direct emitters: separate function ABI preflight,
target-owned frame planning, opcode dispatch and terminators. Preserve ISA
selection in each target backend; share only Core-semantic eligibility
predicates after exact equivalence is proved. Avoid a premature Machine IR.
