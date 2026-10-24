# RV32I upstream minigzip round-trip acceptance

Beyond zlib's 15 unchanged translation units and the untouched
`test/example.c`, now attempt another distinct real-world executable:
the original upstream `test/minigzip.c` at pinned zlib 1.3.1 commit
`51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`.

No generated or simplified minigzip C source is substituted. A single
RV32I/ILP32 translation unit is GNU-preprocessed once; GNU C builds an
independent oracle, and MiniC builds the entire minigzip program.
MiniC's output is assembled through GNU AS and MiniAS independently.

All three executables link the 15 same-mode zlib object files, the
same authentic Picolibc and Linux syscall glue, and run in QEMU.
The test compresses a deterministic 32768-byte binary file, compares
the actual compressed gzip bytes to GNU C's reference, runs the
original minigzip -d program, verifies that every byte matches the
original payload, and checks the expected gzip file cleanup.
This is an operational I/O/compression oracle beyond 'program starts'.
It must return a real success marker before capability is claimed.


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
