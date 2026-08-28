# Shared toolchain libraries

This root is reserved for facts/infrastructure with multiple real consumers.

Expected examples when justified:
- `support/`: small platform-independent support utilities;
- `riscv/`: register/ISA encoding and relocation definitions;
- `elf/`: ELF reader/writer primitives;
- `archive/`: archive-format primitives.

Policy belongs to the tool that owns it. In particular, assembler grammar/directive
semantics, linker resolution policy, compiler lowering policy, and preprocessor macro
semantics must not migrate here merely to avoid duplication.

No speculative shared framework is introduced in M0.
