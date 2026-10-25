# Linux discovery reference matrix

The executable discovery target and the design-reference set are deliberately separate.

## Reproducible driver

- Linux 6.6.143, RISC-V `defconfig`
- Official kernel.org archive pinned by SHA-256 in `probe.sh`
- Purpose: stable, reproducible frontier and regression history

## Design references

For each language/ABI blocker exposed by the pinned driver, inspect the corresponding construct in multiple maintained generations before deciding what MiniC should implement:

- Linux 6.6 longterm family — continuity with the pinned driver
- Linux 6.18 longterm family — newer maintained kernel coding patterns
- Linux 7.1 stable family — current stable-generation coding patterns
- Linux mainline when the construct is actively changing or maintained lines disagree

Compiler-extension semantics are checked against multiple GCC generations as well as current GCC documentation; the pinned CI compiler is not treated as the sole language reference.

A construct is treated as a Linux-specific historical artifact only after comparing these references and the relevant C/GNU ABI documentation. Otherwise the implementation should be a generic compiler capability.

## libc/header references

The Ubuntu 24.04 RV64 cross environment supplies glibc 2.39 headers for executable CI reproduction. Header-driven blockers are also checked against newer stable glibc, currently glibc 2.43, before concluding that a construct is obsolete or version-specific.

Do not patch upstream Linux or libc headers to make MiniC pass.


## Canonical main full-native Linux validation (2026-10-16)

This verification keeps the Linux 6.6.143/GNU control and pinned input
configurations unchanged. It exercises the **current main source**, rather
than replaying an older MiniPP output artifact or hiding a GNU-PP fallback.

- Fresh seven-way **2064/2064 MiniPP -> MiniC -> MiniAS** C-object producer.
  The run must prove actual per-object MiniPP provenance, not just successful
  use of the previously validated GNU-PP MiniC objects.
- Existing native tool-union receiver: MiniAR, native MiniLD Kbuild boundaries,
  native MiniObjcopy, and native non-C Kbuild assembler inputs.
- Retain exact manifest/SHA checks and both GNU reference and QEMU P1 oracles.
  A cache miss or unproven boundary is **not** a success; report it distinctly.
- Separately account for generated-header preprocessing and special linker
  boundaries still delegated to GNU. Do not call the whole kernel completely
  native until every such owner is established.
- Do not change or reinstall historical Python compiler patches: previously
  verified Runtime44/Perf4 fixes are already present in native C under main.

The existing seven-shard workflow is the actual execution surface. Commit
message opt-in tags select its native producer and receiver without adding
workflow files or changing any product C, assembler or preprocessor sources.

## 2026-10-25 current-main exact-source Linux requalification

The certified Linux 6.6.143 RV64 seven-shard / QEMU P1 workflow remains
`.github/workflows/linux-runtime-gcc-baseline.yml`, with seven authentic
Kbuild C-object producers, SHA-bound GNU golden provenance and the original
Image / QEMU P1 receiver. To requalify a refactored compiler, select the
existing current-main profile and force a real C-owner rebuild; historical
cached objects and previous Linux PASS receipts are not current-source proof.
Do not enable historical Python patch overlays or change kernel C/Kconfig.
The opt-in commit initiating this run is an execution request, not a PASS;
only its producer and receiver CI evidence can establish acceptance.
