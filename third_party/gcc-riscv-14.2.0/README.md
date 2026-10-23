# RISC-V libgcc runtime — GCC 14.2.0, optional runtime provider

Original upstream: https://github.com/gcc-mirror/gcc/tree/releases/gcc-14.2.0/libgcc/config/riscv

The three assembly/header files `muldi3.S`, `div.S`, and `riscv-asm.h` are
unmodified upstream GNU libgcc sources (release `releases/gcc-14.2.0`).
`COPYING3` and `COPYING.RUNTIME` are copied from the same GCC release.
License: GPL-3.0-or-later with GCC Runtime Library Exception 3.1.

**This is a third-party runtime implementation, not MiniC compiler code.** The optional `make runtime-rv64i` target and CI compile these
sources with `-march=rv64i -mabi=lp64` to guarantee the exact ISA and psABI
while validating MiniC's standard `__muldi3`, `__divdi3`, `__udivdi3`,
`__moddi3`, and `__umoddi3` calls. Production builds must link a
compatible GCC libgcc or LLVM compiler-rt runtime from the target toolchain;
MiniC emits ABI calls but neither owns nor links these test files itself.
