# MiniLD ordinary-program acceptance ladder (2026-11-04)

MiniAS's Linux acceptance does **not** establish MiniLD acceptance: the
MiniAS production path uses GNU BFD for its final links.  This checkpoint
introduces a separate MiniLD-only final-link differential while preserving
the exact GCC C sources from MiniAS's ordinary-program ladder.

## Reproducible commands

```sh
make -j4 MODE=release CFLAGS=-Werror \
  build/release/bin/minic-as build/release/bin/minic-ld
ALLOW_NATIVE_X86_64=1 make MODE=release check-minild-gcc-runtime-ladder ARCH=x86_64
make MODE=release CFLAGS=-Werror check-minild-x86-crt-got-relax \
  check-minild-multitarget-relocatable check-minild-multitarget-static
```

`probe-minild-gcc-runtime-ladder.sh` runs each stage independently:

1. `exit` (`-O0`);
2. `control` (`-O1`, recursive calls and standard output);
3. `memory` (`-O2`, malloc/memcpy/qsort/free);
4. `multitu` (`-O1`, separate main/helper objects);
5. `io` (`-O2`, libc file I/O).

For every stage, GCC emits assembly, **MiniAS** creates the ET_REL objects,
and the **same objects** are linked independently by GNU BFD and MiniLD,
using the target GCC's real `crt1.o`, `crti.o`, `crtbeginT.o`, static
`libgcc.a`, `libgcc_eh.a`, `libc.a`, `crtend.o` and `crtn.o`.  Real native or
QEMU-user execution must agree in exit status, stdout and stderr.  No
in-memory ELF-only or GNU-linked candidate is accepted.  Complete per-stage
input hashes, diagnostics and outputs are kept under `WORK_DIR`.

The workflow `.github/workflows/minild-gcc-runtime-ladder.yml` is
**manual-only** during initial gap collection, separate from the established
five-target ELF boundary CI.  It installs the actual cross-GCC/sysroot and
QEMU for each architecture and uploads failing artifacts.  No missing QEMU
may be silently counted as a runtime pass.

## Observed acceptance, not a projected pass rate

| Target | Current level | Ordinary five-stage MiniLD result |
|---|---|---|
| x86-64 | GNU BFD native 5/5; MiniLD genuine CRT/GOT probes native PASS | **0/5**, first block is TLS relocations |
| RV64 | Historical MiniLD musl/Lua/QEMU success on a different revision | New GCC/glibc ladder requires CI rerun |
| AArch64 | MiniAS object + MiniLD minimal ET_EXEC, QEMU success in earlier CI | New GCC/glibc ladder requires CI rerun |
| LoongArch64 | MiniAS object + MiniLD minimal ET_EXEC, QEMU success in earlier CI | New GCC/glibc ladder requires CI rerun |
| RV32 | ET_REL and narrow compat-vDSO only | `elf32lriscv -static` is explicitly unsupported |

The original MiniAS RV64 C ladder used `rv64gc/lp64d`.  MiniC's Linux
code-generation contract is separately `rv64i/lp64`; they are **not the same
configuration**.  Set `MINILD_RV64_PROFILE=rv64i-lp64` to make the distinct
ISA/ABI attempt explicit; it requires a compatible LP64 C runtime/sysroot.
The default `gcc-glibc` profile preserves the original `rv64gc/lp64d` ladder.

### Real x86-64 failure progression

1. All five GNU BFD static-glibc outputs linked and ran successfully.
2. MiniLD first rejected the legal GNU `crtn.o` without `.symtab`.
   Section-only, symbol-free ET_REL inputs now enter admission; relocated or
   grouped inputs without a symbol table still fail closed.
3. The next blockers were relaxable GOTPCRELX instructions and then the real
   `R_X86_64_GOTPCREL` (`G+A-P`) relocation.  An x86 target-owned static
   `.got` allocates symbol slots; validated GOTPCRELX opcodes may relax into
   direct references.  The isolated `check-minild-x86-crt-got-relax` probe
   links, executes and GNU-compares **two** programs (CRT/no-symtab/weak GOT
   and non-relaxable true GOT): PASS. A static IFUNC-GOT reference is
   deliberately rejected until IPLT/IRELATIVE support exists.
4. Re-running all five real C stages now stops first at
   `R_X86_64_TPOFF32` (exit) / `R_X86_64_GOTTPOFF` (the other four).
   Five-stage MiniLD status is therefore still **0/5**.

**Do not implement TLS by treating these as ordinary PC-relative fixups.**
Static glibc needs contiguous `.tdata`/`.tbss`, PT_TLS, TLS layout and
offset semantics, and still has IFUNC/IPLT/IRELATIVE initialization paths.
The current generic static layout places all RW PROGBITS before RW NOBITS,
so merely emitting PT_TLS without updating section placement is incorrect.
This is the next coherent architectural change, followed by exact GNU BFD
runtime differential, then cJSON, zlib, Lua, BusyBox, and eventually Linux.

## Verified locally on this revision

- Release MiniAS/MiniLD build with `CFLAGS=-Werror`: PASS.
- Five-target ET_REL boundary: PASS; 10 relocation positive cases and 20
  negative cases.
- Three-target static ELF baseline: PASS including 19 negative cases and
  x86 native/GNU differential; AArch64/LoongArch64 **runtime unverified
  locally** because cross-QEMU is absent.
- x86 CRT/no-symtab + GOTPCREL(X) isolated programs: 4 actual native runs,
  two GNU/MiniLD parity checks, PASS.
- New x86 full glibc five-stage ladder: GNU 5/5, MiniLD 0/5; TLS blockers
  retained in `build/minild-gcc-runtime-ladder/x86_64/**/evidence`.
