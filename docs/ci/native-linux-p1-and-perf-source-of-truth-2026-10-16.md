# Native Linux acceptance and CI performance source of truth

Date: 2026-10-16. Target: Linux 6.6.143 RISC-V, pinned GNU13
golden Kbuild object cache, MiniPP + MiniC + MiniAS + native auxiliary tools.

## Accepted production runtime

**Verified immutable commit**:
`819751e75e333bc05983f5989595a09493d5124a`.

**Actual full seven-worker and QEMU run**:
https://github.com/yituanxing/minic-toolchain/actions/runs/38852534422

Proven results (from real CI stdout, NOT earlier historical predictions):

- Nine frozen target macro contexts are byte-identical between native C
  MiniPP producer and the historical Python test oracle.
- Seven actual full shards: **2064/2064 Linux C objects** produced through
  MiniPP `.i` → MiniC `.s` → MiniAS `.o`, hash-authenticated and merged.
  Neither the historical Python producer nor GNU's reference `.i`
  is consumed by native MiniC.
- Another **34 native S objects** participated through MiniAS. Three
  Kbuild-derived S objects were correctly restored after first link.
- **QEMU P1 12/12** after two-stage Linux linking with the original
  16-KiB stack. Exact CI marker:
  `COHORT_P1=PASS name=full_all_verified syscalls=12`.
- Native MiniAR **643 calls**, including two memberless `mPiT`
  archive moves, zero GNU AR calls.
- Native MiniLD ELF64 vDSO with seven versioned exports, direct
  Kbuild `vdso.so.dbg` and native MiniObjcopy; zero GNU fallback
  for 64-bit vDSO.
- Native intermediate `vmlinux.o` (-r), purgatory relocatable and final
  static links, the six final `vmlinux/kallsyms` links; total
  **11 actual MiniLD links** in native-union trace.
- There were **three GNU LD invocations** in the same real Kbuild trace:
  two transient feature probes using temporary objects (not kernel
  outputs), plus **one actual ELF32 compat-vDSO link**. ELF32 output
  remains genuinely unsupported by MiniLD (initial error
  `unsupported-emulation:elf32lriscv`); do not relabel it as accepted.
- The measured 11 MiniLD calls summed to **12,699 ms process wall time**:
  6 final links 9,477 ms; 2 `-r` links 3,209 ms; vDSO64 3 ms;
  purgatory `-r` 5 ms; purgatory static 5 ms. These are cumulative
  link-call milliseconds, **not end-to-end CI wall time** and not a
  same-run GNU-vs-MiniLD speedup estimate.

Full-run proof is tied to the immutable source SHA above, not to any
future unverified C compiler edits. `src/compiler/compiler.c` was
restored to the pinned certified blob
`1424ae500d9662068612b63091691ce8f322b371` after a temporary,
unaccepted C-level profiling experiment tripped the legitimate
`MINIC_NATIVE_PROFILE` hash gate. Do not bypass that hash gate to
make a profiler look green.

## Production Python convergence

The product-owned native rule producer is
`preprocessor/target/riscv_linux_predefines.c`; it is compiled into
`minic-cpp`. The exact original Python rule logic survives solely as
`tests/preprocessor/oracle_riscv_linux_predefines.py` for regression.
The immutable GCC macro snapshot remains a test fixture. No Python/GNU
process is required to synthesize the nine accepted target macro
contexts in the native production path.

## Fast performance feedback and evidence semantics

The existing Linux seven-way producer records, per genuine C owner:

- Independent `MiniPP`, `MiniC`, `MiniAS` child wall times.
- Linux `wait4` ru_maxrss per child, and user/system CPU in each owner log.
- Exact `.i`, `.s`, `.o` output sizes and source/tool SHA identities.
- A per-shard slowest-five owner list; full merge additionally writes
  `native-performance.json` with stage distributions and top-20 hot
  owners. Missing measurements are explicitly flagged, never guessed.

Sampling run #38853207117: **7 × 32 = 224/224 real owners** passed
using the native C rule generator. Across those 224 owners the
**per-child wall-time sums** were MiniPP **102.69 s**, MiniC
**260.79 s**, MiniAS **24.34 s**. Because workers run concurrently,
these are **not** total CI duration. The slowest observed MiniC owner
was `kernel/events/core.o` (7.107 s, 370040 KiB peak child RSS).
High-water RSS is process-level, not memory allocated within one
compiler phase.

Full-mode shards now fail fast on the **first real owner error** and
retain `first-failure.json` and existing stderr/command/ABI evidence.
Focused syntax checks precede expensive Linux runs. All data reuse the
existing seven jobs; no new workflow, extra compiler invocation, or
GCC oracle output replacement is necessary.

## Remaining scope, without false completion

1. **Actual ELF32 compat-vDSO linking** in MiniLD is a separate
   implementation gate. Four genuine ELF32 ET_REL objects, their
   relocations, linker script, and a GNU diagnostic-only ET_DYN reference
   are captured by the existing cached replay. Do not accept an ELF64
   output labeled ELF32, nor feed GNU's output into the native pipeline.
2. The existing link-feature probes may remain GNU without preventing
   an otherwise native product (they are not product links).
3. C-level phase instrumentation was deliberately **not** promoted over
   an accepted compiler source hash. Repeat full 2064+QEMU certification
   after any future production compiler-source change.
4. The other four preserved branch refs are ancestors of `main`
   (`behind_by=0` for each when compared to `main`). Their code is
   not missing from the canonical branch; branch-ref deletion is
   separate repository housekeeping, not a missing merge.
