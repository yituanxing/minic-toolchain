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
