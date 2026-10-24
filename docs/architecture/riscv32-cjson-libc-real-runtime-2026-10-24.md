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
`sbrk` and process exit. C library functionality remains upstream.
All three QEMU ELF32 executions must exit zero and stdout/stderr
must match. Failures fail the job and do not print acceptance.

Historical RV32 cJSON compile/ELF32 gates remain intact until this
independent runtime proof is green, after which the frontier can
become a mandatory successful real-program gate.
