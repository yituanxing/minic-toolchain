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
make MODE=release CFLAGS=-Werror check-minild-rv32-static-runtime
# CI only: needs genuine RV32I Picolibc multilib, GNU BFD and QEMU.
make MODE=release check-minild-rv32-picolibc-ladder
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
**manually dispatchable**, and changing that workflow file triggers an
independent five-target exploratory cohort on the refactor branch. Ordinary
linker-only edits do **not** repeatedly launch it, separate from the established
five-target ELF boundary CI. It installs the actual cross-GCC/sysroot and
QEMU for each architecture and uploads failing artifacts. RV32's ILP32 runtime
is **real Debian Picolibc with the project's Linux syscall bridge**, not the
ABI-incompatible RV64 glibc; its five C stages use the separate
`probe-rv32-picolibc-ladder.sh` while preserving GNU BFD/MiniLD parity.
No missing QEMU may be silently counted as a runtime pass.

## Observed acceptance, not a projected pass rate

| Target | Current level | Ordinary five-stage MiniLD result |
|---|---|---|
| x86-64 | GNU BFD + MiniLD native CRT/TLS/IFUNC, five ordinary C stages | **5/5 PASS** with native execution and GNU output parity |
| RV64 | RISC-V TLS and ULEB128 GNU BFD/QEMU differential | **5/5 PASS** with real glibc and QEMU output parity |
| AArch64 | GOT/TLS IE/LE and static IFUNC GNU BFD/QEMU differential | **0/5**, all real glibc stages MiniLD-link but QEMU exit 139; GNU works; the current fault is a discontinuous `.init_array` range |
| LoongArch64 | GOT/TLS, true ALIGN-shrink, PCALA K12 and undefweak B26 GNU BFD/QEMU differential | **0/5**, all real glibc stages MiniLD-link but QEMU exit 139; GNU works; the current output lacks static IFUNC relocation synthesis |
| RV32 | ELF32/ILP32, TLS LE and ULEB128 GNU BFD/QEMU differential | **5/5 PASS** with real RV32I Picolibc, Linux-syscall bridge and QEMU output parity |

The first real GCC/libc CI cohort is
[`41830734058`](https://github.com/yituanxing/minic-toolchain/actions/runs/41830734058).
Its five jobs completed: x86-64 succeeded and the other four reported
precise linker diagnostics above.  The independently passing ELF boundary
workflow is
[`41830435620`](https://github.com/yituanxing/minic-toolchain/actions/runs/41830435620):
five ELF relocatable targets, three trivial static ELF targets, RV32 GNU/QEMU,
and x86 CRT/IFUNC/GOT parity are genuinely covered.  It does **not** execute
the five real libc workloads on every architecture.

The next cohort,
[`41832776917`](https://github.com/yituanxing/minic-toolchain/actions/runs/41832776917)
at refactor commit `930581e8`, confirmed that the first blockers were actually
removed.  x86-64 still passed **5/5**; the other four again had **0/5** real
libc acceptance, but exposed deeper, different first errors:

| Target | New observed blocking frontier |
|---|---|
| RV32 | `R_RISCV_SET_ULEB128` (type 60), including real Picolibc debug data |
| RV64 | `__ehdr_start` runtime-defined ELF header symbol (and type 60 at `io`) |
| AArch64 | New TLS IE/LE structural test passed; GNU BFD rejected a fixture mixing TLS and non-TLS relocations, so the actual libc ladder did not execute in this cohort |
| LoongArch64 | `R_LARCH_ALIGN` needs real section relaxation (`.text:0:alignment=8:reserved=4`) |

In particular, an invalid GNU oracle fixture is not evidence of MiniLD
glibc failure or success.  These are failure-first diagnostics, not claims
of a completed five-architecture user-mode program runtime.

The third cohort, [CI #41835291488](https://github.com/yituanxing/minic-toolchain/actions/runs/41835291488),
commit `a35cc6a`, is the first to reach three complete five-stage ordinary
program runtime passes: **x86-64 glibc 5/5, RV64 glibc 5/5, and RV32I
Picolibc 5/5**.  The same CI observed AArch64 real-glibc output links but
immediate QEMU status 139 at all five stages; LoongArch64 real-glibc output
links fail at `nonriscv-relocation-overflow-or-encoding:258:71`.  Its separate
LoongArch64 GNU/QEMU two-ALIGN-shrink and AArch64 GNU/QEMU TLS IE/LE target
probes both passed.  The paired five-target ELF boundary CI
[#41835291703](https://github.com/yituanxing/minic-toolchain/actions/runs/41835291703)
also completed successfully. These structural and library workloads are
distinct from Linux kernel `vmlinux` final linking.

The latest fifth-stage ordinary-program cohort,
[CI #41838512487](https://github.com/yituanxing/minic-toolchain/actions/runs/41838512487)
at refactor commit `2de4541`, independently reconfirmed x86-64/RV32/RV64
at **5/5** each. It advanced LoongArch64 from unresolved `PCALA_HI20` /
undefweak `B26` / `PCALA_LO12` linker relocations to **five produced static
glibc ELF executables**, while all five still crashed in QEMU (`139`). Its
new LoongArch GNU BFD/QEMU ISA differential tests passed. All five AArch64
glibc programs also linked but returned QEMU `139`; bounded real CPU tracing
identified the first `__libc_start_main` crash (`PC=0x400630`, `si_addr=NULL`).
An exact binary comparison then showed the MiniLD `.init_array` at
`0x47c1b0` separated from `.init_array.00090` at `0x481298`, while its
`__init_array_start/end` symbols incorrectly span the intervening gap.
glibc calls the NULL second entry. GNU BFD packs those entries contiguously.
This is a generic static-layout correctness bug, **not** evidence that the
IFUNC relocation probe was wrong. Independently, the LoongArch GNU reference
contains 14 `R_LARCH_IRELATIVE` records and valid `.iplt`/`.igot.plt`,
while MiniLD's static glibc executable contains none; the LoongArch target
must synthesize its IFUNC resolver records before real execution can pass.

The five-target ELF boundary CI remained green in both
[#41838512398](https://github.com/yituanxing/minic-toolchain/actions/runs/41838512398)
and [#41838522839](https://github.com/yituanxing/minic-toolchain/actions/runs/41838522839).
Passing boundary CI and successfully writing five ELF executables are
necessary milestones, but neither is a substitute for QEMU runtime parity.

### Separate Linux kernel link acceptance

Keep four levels as separately scored outcomes:

1. MiniAS `.s` to ET_REL and MiniLD `-r`, with GNU oracle and ELF checks;
2. MiniLD `-static` basic non-libc executable, with QEMU runtime parity;
3. real GCC/CRT/libc application, GNU BFD versus MiniLD on identical objects,
   both independently executed and compared (the five-stage matrix above);
4. Linux kernel **final** link by MiniLD (`vmlinux`/Image), ELF/relocation
   inspection and actual QEMU boot.  A Linux build where GNU BFD performs the
   final link must be labeled `GNU final`, regardless of MiniLD intermediate
   `-r` success.

Historical `minild-integration-v1.yml` RV64 `linux-rel-boundaries` tests only
replaced two **intermediate `-r` links**; GNU BFD linked their eventual
`vmlinux`.  That specific workflow cannot count as MiniLD-final acceptance.
However, the *separate* real Kbuild native-union workflow subsequently
certified RV64 MiniLD Linux final links and actual QEMU P1.  Strongest
record: [CI #38542731056](https://github.com/yituanxing/minic-toolchain/actions/runs/38542731056),
commit `a44067096709ee51db4ed113202ceb4eec0b3af0`, documented in
`docs/ci/linux-native-vdso64-p1-certified-2026-10-14.md`: six actual
MiniLD `vmlinux` kallsyms/final links, MiniLD `vmlinux.o -r`, purgatory
and real ELF64 vDSO shared link; GNU BFD still performed the single RV32
compat-vDSO shared link.  The 12/12 P1 syscall oracle passed.  This
historical RV64 success has **not** been repeated on the present refactor
head and does **not** establish five-architecture MiniLD Linux-final success.

At this refactor head, `linker/src/linker_target_elf.inc` advertises
`supports_static_script=true` and `supports_static_gc=true` for RV64, but
both features are still `false` for RV32, x86-64, AArch64 and LoongArch64.
Those four targets therefore cannot yet accept the actual Kbuild
`-T vmlinux.lds` final-link command, even if their ordinary `-static`
application ladders later pass.  Do not conflate the existing ET_EXEC/ET_REL
target support with linker-script/GC support.  The smallest historic RV64
final-link replay is `tools/ci/minild-linux-cache-final-replay-v1.sh`, which
reuses pinned GNU-object caches for a single kallsyms target rather than
recompiling all 2,064 Linux C sources.
The refactor-specific `.github/workflows/minild-rv64-linux-cached-final-recert.yml`
uses the historical exact GNU golden cache key, verifies its original GCC
object SHA manifest, and replays only that one link. A cache miss is a hard
blocker, **not** permission to rebuild 2,064 C inputs. Even a successful
single cached final link is not a fresh QEMU P1 certification.
The first cache-only check,
[#41836643714](https://github.com/yituanxing/minic-toolchain/actions/runs/41836643714),
failed closed at `actions/cache/restore` because the exact historical cache
key was absent. No Linux C files were rebuilt and no current-head Linux
link success was claimed. This check is now manual-only to avoid repeatedly
installing a cross-toolchain for a known-missing cache on every source push.

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
4. The next failures were `R_X86_64_TPOFF32` / `R_X86_64_GOTTPOFF` and then
   static glibc IFUNC initialization. TLS is now placed as a contiguous
   `.tdata`/`.tbss` block, with `PT_TLS` and target-owned x86 Variant II
   offsets.  `R_X86_64_IRELATIVE` records, IPLT/IGOT slots and live
   `__rela_iplt_start/end` are synthesized so glibc's actual startup can
   resolve IFUNC before it calls optimized libc functions.  Unsupported
   IFUNC GOT forms still fail closed.
5. On the local host, **all five** GCC→MiniAS→MiniLD static-glibc executables
   now link and run; GNU BFD links the exact same objects, and exit/stdout/
   stderr agree for `exit`, `control`, `memory`, `multitu` and `io`.

The next milestones remain exact five-architecture CI/QEMU differential,
then cJSON, zlib, Lua, BusyBox, and eventually Linux. In particular, one
x86-64 5/5 does **not** establish five-architecture Linux correctness.

## Verified locally on this revision

- Release MiniAS/MiniLD build with `CFLAGS=-Werror`: PASS.
- Five-target ET_REL boundary: PASS; 10 relocation positive cases and 20
  negative cases.
- Three-target static ELF baseline: PASS including 19 negative cases and
  x86 native/GNU differential; AArch64/LoongArch64 **runtime unverified
  locally** because cross-QEMU is absent.
- x86 CRT/no-symtab + GOTPCREL(X) isolated programs: 4 actual native runs,
  two GNU/MiniLD parity checks, PASS.
- New x86 full glibc five-stage ladder: GNU 5/5, MiniLD 5/5, native runtime
  differential 5/5; TLS and IFUNC evidence retained in
  `build/minild-gcc-runtime-ladder/x86_64/**/evidence`.
- RV32 ELF32 `-static` two-stage/roundtrip structural checks, GOT width and
  invalid relocation rejection: PASS locally. The host lacks RV32 GNU BFD
  and QEMU, but the independent **real RV32I Picolibc 5/5 GNU/QEMU** job
  passed in [CI #41838512487](https://github.com/yituanxing/minic-toolchain/actions/runs/41838512487).
