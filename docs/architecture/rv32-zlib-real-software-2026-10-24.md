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
