# RV32I variadic callee ownership and differential

Real original zlib 1.3.1 gzwrite.c contains gzprintf(...),
requiring a genuine RISC-V ILP32 va_list and ABI register-save area.

The previously shared RISC-V GPR cursor now supplies a single
per-function named-parameter prefix. RV32 reserves the unsaved
argument registers aN..a7 immediately below entry sp (the caller's
overflow stack area). It writes each register once at entry, and
Core's VARIADIC_ARGUMENT_ADDRESS exposes either the first saved
register or the first stack-passed unnamed argument after the named
stack words.

The RV32I 4-byte linear scalar va_arg accepts one-word int and pointer.
It supports va_copy and va_end. Two-word builtin va_arg is still
explicitly rejected in target-aware Core lowering: advancing by
only four bytes for an eight-byte argument would corrupt the cursor.
Forwarding the native va_list pointer into genuine Picolibc
vsnprintf is independent of builtins and can consume double-word
values using Picolibc's own correctly compiled GCC implementation.

The independent GNU C reference, MiniC+GNU-AS and MiniC+MiniAS
ELF32 QEMU test probes forwarding, repeated va_arg on register
and stack areas and va_copy. The real zlib 15/15 TU and unmodified
upstream example remain the target real-software acceptance.
