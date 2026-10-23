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
compatible GCC libgcc or LLVM compiler-rt runtime from the target toolchain.
The optional archive also packages unmodified LLVM compiler-rt 19.1.7
software-floating sources from ../llvm-compiler-rt-19.1.7 under their
separate Apache/LLVM runtime license. It remains opt-in at final link.

The unmodified `stdint-gcc.h` is also pinned from GCC 14.2.0
`gcc/ginclude/stdint-gcc.h` under the same GPL-3.0-or-later / GCC
Runtime Library Exception 3.1. `stdint.h` is a tiny local forwarding shim
to select the upstream GCC constant-width definitions for the freestanding
LLVM C library, and contains no arithmetic algorithms.
