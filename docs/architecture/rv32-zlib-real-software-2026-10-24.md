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
