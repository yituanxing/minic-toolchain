# MiniC Compiler V1 — Frozen Milestone

Status: **FROZEN / maintenance mode**

This document closes the active compiler-construction phase. From this point
forward, the compiler is treated as an input to the Linux/toolchain integration
phase rather than as an open-ended language-implementation project.

## Frozen production architecture

The production function-body path is:

```
normalized Semantic AST
    -> Core lowering
    -> Core IR + semantic metadata
    -> target ABI / DataLayout
    -> RV64 codegen
```

The legacy AST -> RV64 function-body route is not a production fallback.

Ownership boundaries remain:

- parser: syntax and token movement;
- declaration/sema: entity, type, storage and linkage semantics;
- Core: sole production function-body semantic IR;
- target: ABI, DataLayout and RV64 code generation.

## Acceptance evidence

### Linux compiler frontier

- frozen Linux 6.6.143 translation-unit corpus: **3352 / 3352 compile PASS**;
- spread Linux assembler sample: **112 / 112 .i -> .s -> .o PASS**;
- the former `kernel/configs.c` assembler-only failure was classified as a
  frozen-corpus environment issue: the source contains
  `.incbin "kernel/config_data.gz"`, and the sidecar is Kbuild-generated;
- real Kbuild reconstruction proves both `init/main.o` and
  `kernel/configs.o` through the MiniC compiler boundary when the real
  `kernel/config_data.gz` exists.

### Self-host bootstrap

B1 sharded bootstrap run **33176434409** completed successfully.

Acceptance contract:

- Stage0 builds the Stage1 RV64 MiniC compiler;
- Stage1 executes under qemu-riscv64;
- 46 selected compiler implementation translation units are recompiled by
  Stage1;
- **16 / 16 replay shards PASS**;
- all 46 Stage1-generated assembly files are byte-identical to the Stage0
  outputs;
- Stage1 and Stage2 loadable machine images are byte-identical;
- raw ELF files may differ only in non-loadable linker metadata;
- Stage2 executes and compiles the `return_42` smoke correctly;
- Stage2 target runtime gates PASS.

The largest previous self-host performance blocker,
`src/core/core_lower.c`, was reduced from a 600-second timeout to about
21 seconds by removing repeated RV64 emitter scans and caching/precomputing
Core frame object offsets.

### Stage2 real-program runtime

B2 runtime run **33176774385** completed successfully using the frozen Stage2
artifact rather than rebuilding a host MiniC.

Stage2 successfully compiled and ran:

- compiler/program micro runtime;
- tiny-AES;
- cJSON;
- SDS;
- Parson;
- linenoise;
- Lua 5.5.

This is the compiler V1 runtime acceptance boundary.

### Linux runtime contract recovery

The historical Python-era Linux runtime methodology has been restored as a
deterministic QEMU gate.

The GCC Linux 6.6.143 reference Image passes both lanes:

- `rdinit=/init`;
- `rdinit=/bin/sh`.

The gate checks kernel identity, initramfs/devtmpfs setup, PID1 handoff,
`/proc/cmdline`, shell command execution and deterministic user-space
completion markers. This gate is the runtime oracle for the next phase:
Stage2-built Linux.

## Freeze policy

The compiler is now in maintenance mode.

Changes are allowed only when one of the following is true:

1. a Stage2, Linux Kbuild, link, boot or runtime gate exposes a reproducible
   compiler correctness defect;
2. a compiler performance defect prevents a required validation gate from
   completing in a practical bounded time;
3. a narrowly scoped observability change is required to diagnose either of
   the above.

The following are **not** active goals:

- adding C features for completeness without a concrete blocker;
- speculative compiler redesign;
- cosmetic backend rewrites;
- broad optimization work unrelated to a measured integration bottleneck.

## Next phase

The primary project line is now:

```
frozen Stage2 MiniC
    -> Linux frozen replay
    -> full Linux Kbuild
    -> vmlinux / Image
    -> QEMU + OpenSBI boot
    -> Python-era runtime contract
    -> deeper system/runtime regression
```

Compiler work resumes only when this integration line proves that it must.
