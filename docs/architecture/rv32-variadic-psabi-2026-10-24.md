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


## Accepted real software — verified 2026-10-24

The dedicated job completed successfully:
[Actions run 40104487724](https://github.com/yituanxing/minic-toolchain/actions/runs/40104487724).
The evidence includes:

```text
RV32_VARIADIC=PASS GCC=1 MiniC=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1
RV32_ZLIB_TU_SUMMARY total=15 compiled=15 blocked=0
RV32_ZLIB=PASS original_upstream=1 TUs=15 MiniC_only=1 GNU_C_oracle=1 GNU_AS=1 MiniAS=1 Picolibc=1 ELF32=1 QEMU=1 example=byte_exact
RV32_MINIGZIP_COMPILE=PASS upstream_original=1 GCC=1 MiniC=1 GNU_AS=1 MiniAS=1
RV32_MINIGZIP=PASS original_upstream=1 unchanged_zlib_TUs=15 full_MiniC=1 GCC_oracle=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1 gzip_compress=1 gzip_decompress=1 gzip_bytes_equal=1 original_bytes_restored=1
```

All 15 unchanged zlib 1.3.1 translation units were compiled by MiniC,
assembled with GNU AS and MiniAS, and linked into the original
`test/example.c` and `test/minigzip.c`. Separate GCC-compiled
versions provided independent reference executables. Under RV32I QEMU,
the examples exited normally with byte-exact stdout/stderr. Real
minigzip compressed and decompressed a 32,768-byte binary file, and
both MiniC gzip streams matched GNU's stream byte-for-byte.

The original gzwrite.c variadic `gzprintf` now uses an actual
ILP32 aN..a7 save area, and the external real-Picolibc errno API is
bridged without stripping its TLS qualifier. Correct Linux RV32
`llseek` syscall 62 requires five argument registers and an output
pointer; it is not the three-argument LP64 lseek syscall. The separate
real-libc / ELF32 adapter makes this boundary explicit.

This validates the observed upstream programs and runtime paths;
general 64-bit builtin `va_arg`, native MiniC TLS code generation,
other zlib configurations and complete RV32 C language support are
not yet claimed.
