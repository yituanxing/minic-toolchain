# AArch64 vertical slice: prove the target boundary

`minic-as --target=aarch64 [-march=armv8-a] [-mabi=lp64]`
is an intentionally **limited** target that proves that MiniAS common parsing,
section/symbol management, target-owned machine encoders, relocation storage
and generic ELF64 writer work for a *second ISA*. It is not yet a complete
AArch64 assembler, and it is not a MiniC backend.

Implemented A64 instructions: `nop`, `ret`, `ret xN`, `mov xN,#imm16`,
`mov wN,#imm16`, `svc #imm16`, `b symbol`, `bl symbol`.
The relocation types are ABS32, ABS64, JUMP26 and CALL26.
AArch64 `#` immediates and `//` comments use architecture-specific lexical
rules; executable NOP filling is target-owned. Unsupported instructions and
unsupported ISA/ABI requests fail closed.

`tests/assembler/probe-aarch64-elf64.sh` compares A64 code bytes to GNU AS,
checks relocation types and ELF machine, links using the genuine AArch64 GNU
linker and runs a real Linux exit syscall under QEMU AArch64.

Remaining: full AArch64 grammar, aliases and relocations, feature descriptors,
proper conditional branches, local fixups and relaxation, complete CFI,
ELF attributes and GNU differential corpus. No Linux kernel support claimed.
