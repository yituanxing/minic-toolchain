# Native C Toolchain Module Roadmap

Status date: 2026-08-28

The native rewrite is developed as independent, freezeable modules.  A module
does not stay permanently open just because later integration exposes new work.

## 1. Compiler — FROZEN

Milestone: [compiler-v1-frozen.md](compiler-v1-frozen.md)

Acceptance already established:

- Core-only production function-body path;
- Linux frozen translation-unit coverage;
- Stage0 -> Stage1 -> Stage2 self-host fixed point;
- Stage2 real-program runtime;
- Stage2 Linux spread sample validation;
- Linux runtime validation framework restored.

Compiler changes after freeze are limited to reproducible correctness blockers,
validation-blocking performance defects, or diagnostic instrumentation.

## 2. Preprocessor — NEXT

The current compiler accepts preprocessed input well enough that preprocessing
can be developed as a separate module.

### Required architecture boundary

Initial native-C stages should be:

1. file buffer / mapped-file abstraction;
2. compact preprocessing token array;
3. include resolver and canonical path ownership;
4. include-guard / pragma-once metadata;
5. raw-header cache;
6. directive engine;
7. object-like and function-like macro expansion;
8. hide-set / rescan semantics;
9. stringization, token paste and variadics;
10. GNU/Linux compatibility extensions actually observed by the frozen corpus.

Do not start by translating the old Python string-rescan implementation line by
line.  The native implementation should make token ownership and rescan state
explicit.

### Oracle

The Python V218-era preprocessor remains the semantic oracle.

Promotion order:

```
focused directive/macro differential
  -> real-header differential
  -> Linux spread sample
  -> frozen Linux full .i byte-exact gate
  -> compiler object/runtime regression
```

A preprocessor stage is not promoted on parse success alone.  Where the Python
oracle is deterministic, emitted `.i` must be byte-exact after normalization
of deliberately excluded path/timestamp fields.

### Performance rule

Measure mixed Linux workloads, not only tiny headers.  Macro expansion/rescan
and repeated header processing are the first-order targets; cache design must
store compact native data (bytes/tokens/path/guard metadata), not reconstructed
source strings.

## 3. Assembler — after preprocessor

Replace the current GNU assembler boundary only after preprocessing is stable.
Use GNU RISC-V assembler output as the instruction/relocation oracle and retain
focused encoding + relocation differential gates before Linux integration.

## 4. Linker and binary utilities

Then move the remaining Python-era target tools behind the same freeze model:

- linker;
- archive;
- nm;
- objcopy;
- strip;
- objdump.

Each tool gets a narrow semantic oracle first, Linux/Kbuild integration second,
and runtime evidence last.

## Integration principle

The mainline progression is therefore:

```
Compiler V1 [FROZEN]
  -> Preprocessor
  -> Assembler
  -> Linker
  -> Binary utilities
  -> native end-to-end Linux toolchain
```

Do not reopen a completed module unless a downstream gate produces concrete
evidence that its contract is insufficient.
