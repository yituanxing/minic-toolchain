# RV32I zlib 1.3.1 original 15-TU qualification

Next genuine software following unchanged tinyAES/cJSON: pinned
zlib 1.3.1 commit `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`.
The 15 standard zlib translation units are compiled **all by MiniC**,
twice assembled (GNU AS, MiniAS) and independently compiled by GCC
from their identical RV32I preprocessed inputs. There is no fallback
from MiniC to GCC for any zlib function and no modified zlib C source.

A single already-provisioned Picolibc/GNU/QEMU job tests all 15 in one
batch. The existing pinned `test/example.c` from zlib is the first
real-program functional oracle. Original Picolibc provides malloc and
stdio. Linux integration extends the same bridge as cJSON with Linux
read/openat/close/lseek/unlink syscalls; there are no custom libc or
compression algorithms. GNU and MiniC binaries share the same example,
CRT, POSIX syscall adapter, Picolibc and libgcc.

A failed compilation must report all 15 first blockers and terminate
with a nonzero CI result. QEMU exit and stdout/stderr for both MiniC
assembly paths must match the independent original GNU C reference.
Existing fourarch and cJSON regressions must remain green.

Potential subsequent acceptance: unmodified upstream minigzip and
cross-gzip byte identity, only after this original upstream example
is fully qualified.

## Picolibc TLS errno interface

The pinned Debian Picolibc headers expose either `extern __thread int errno`
or the documented `__PICOLIBC_ERRNO_FUNCTION` API. MiniC already
recognizes opaque extern TLS declarations but intentionally rejects TLS
memory access until true TLS relocation lowering exists. zlib's gzread
and gzwrite use errno on I/O error paths. This workload therefore
selects the **actual Picolibc-supported errno-function ABI** at the
GNU preprocessing stage for *both* GCC reference and MiniC, and uses
one GCC-built `__minic_picolibc_errno()` wrapper returning the address
of the actual TLS errno object. No library source is patched, no errno
is replaced by a fake global, and MiniC does not silently perform an
incorrect non-TLS load. This is a declared runtime ABI adaptation, not
proof MiniC supports general C TLS access.

## Linux RV32 llseek correction

The first original GCC zlib example QEMU run uncovered a *porting*
error, not a MiniC code-generation regression. Linux RV32 syscall 62
is `llseek(fd, offset_high, offset_low, result_ptr, whence)`, whereas
LP64 uses the three-argument lseek form. Our initial platform bridge
mistakenly passed only three arguments, yielding EINVAL and then an
uninitialized Picolibc TLS errno access. The bridge now uses the
correct five-register Linux compat ABI and returns the 64-bit result
through a kernel output pointer, range-checking 32-bit off_t. The
original zlib code and MiniC IR remain unchanged.


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
