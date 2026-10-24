# cJSON 1.7.19 RV32I/ILP32 runtime qualification

The MiniC ABI, true two-word scalar arithmetic and original LLVM/GNU
integer/FP runtime are now qualified. A full cJSON executable needs a
**real RV32I soft-float libc**, not an RV64 glibc, and must execute
the unchanged cJSON harness. The system Ubuntu 24.04 package
`picolibc-riscv64-unknown-elf` provides RISC-V RV32I/ILP32 libraries
with `gcc-riscv64-unknown-elf`. It is a genuine upstream allocator and
parsing/formatting/math provider, not a fake MiniC shim.

The dedicated slow but isolated CI job does not delay the fourarch
runtime suite. It rebuilds only MiniC/MiniAS once and uses the
existing frozen source integrity and preprocessing path. Original
cJSON is compiled (a) entirely with GNU C as oracle, and (b) entirely
with MiniC, assembled separately by GNU AS and MiniAS. The *same*
original cJSON runtime_harness.c, Picolibc and Linux RV32I startup
and OS-syscall adapter are linked into all executables. No source
function is delegated from MiniC to GNU C.

The Linux-specific glue is exactly process entry, `brk`-based
`sbrk`, Linux write(2) and process exit; the upstream libc owns
stdio formatting, output buffers and shutdown flushing. C library functionality remains upstream.
Both the unchanged existing `runtime_harness.c` and the original
upstream `test.c` are compiled with the same independent GNU compiler.
For each workload, all three QEMU ELF32 executions (GCC reference,
MiniC + GNU AS, MiniC + MiniAS) must exit zero and stdout/stderr must
match byte-for-byte. Failures fail the job and do not print acceptance.

Historical RV32 cJSON compile/ELF32 gates remain intact until this
independent runtime proof is green, after which the frontier can
become a mandatory successful real-program gate.

The upstream `test.c` exercises `printf`, and this package builds
Picolibc with tinystdio's standard stream pointers left as platform
integration points. The fixture implements the documented Picolibc
`FDEV_SETUP_STREAM` console using genuine Linux write(2), with real
stdio formatting still entirely inside Picolibc. This is a *stream
device* binding, not an alternative C library implementation.
