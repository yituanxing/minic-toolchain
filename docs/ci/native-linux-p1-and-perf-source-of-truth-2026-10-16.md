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

## 2026-10-16: RV32 ELF32 native compat-vDSO link qualification

**Certified isolated four-object Linux link**:
[CI #38856103863](https://github.com/yituanxing/minic-toolchain/actions/runs/38856103863);
native writer at `linker/src/elf32_compat_vdso.c`, CLI dispatch for
`-melf32lriscv -shared` at `tools/minic-ld/main_core.inc`.

- Actual frozen Linux 6.6.143 four ELF32 ET_REL objects and the generated
  Kbuild linker script used directly. Native MiniLD emitted a **real
  ELF32 ET_DYN**, **3048 bytes**, vs diagnostic-only GNU LD reference
  **3600 bytes**. No GNU fallback or GNU output repackaging.
- Same four object inputs produce **32 identical `.text` bytes**,
  all three `__vdso_*@@LINUX_4.15` dynamic exports, real
  `PT_LOAD`, `PT_DYNAMIC`, `PT_NOTE`, `PT_GNU_EH_FRAME`,
  SYSV and GNU hash tables, real unwind records, Linux note,
  and a native SHA1 GNU build-id. Different valid ELF layouts and
  unwind CIE folding do **not** imply identical output bytes.
- Actual CI #38856103863 reported `MINILD_VDSO32=PASS`
  `exact_Linux_ELF32_link=true`, with GNU diagnostic wall
  `0.00s`/RSS `4624 KiB`, native `0.00s`/RSS `1988 KiB`
  (centisecond timing resolution is **insufficient to infer speedup**).
  No Linux C object recompilation was involved.
- An early bug in native GNU-hash chain offsets caused an actual CI
  process crash (rc 139); fixed in the C writer and revalidated
  with the real frozen objects. An ELF32 `SHT_SYMTAB` extraneous
  local entry was subsequently removed.
- Scope deliberately **limited to this real Linux four-object ABI**.
  Full RV32 compiler/backend/assembler and general-purpose ELF32
  linker are explicitly out of scope. Kbuild and P1 integration
  are *not* proven by a cached, independent four-object link.

**Full integration gate**:
[CI #38857454218](https://github.com/yituanxing/minic-toolchain/actions/runs/38857454218),
SHA `bcb8a46a4249ce7e571e074079a31c7ec1695ead`.
This run is the sole current-source seven-way full producer with the
native-ELF32 opt-in on the already existing Linux receiver.
**Record as pending until the exact real Kbuild native link, GNU
fallback count zero, generated compat-vDSO outputs, and QEMU P1 12/12
are observed.** Prior successful RV64 P1 cannot substitute for
this new linker SHA.

Cache lesson: the historical full MiniC Actions cache is keyed to
an **earlier compiler source profile**, so a cache-only
`[linux-native-union-cached]` attempt reported
`Failed to restore cache entry` correctly. Do **not**
bypass the SHA key just because existing 2064 objects passed earlier.
The original `Makefile` was restored byte-for-byte to avoid
unnecessarily invalidating the compiler key when merely adding a
new MiniLD file; the writer is currently included through
`tools/minic-ld/main.c` into the existing MiniLD TU pending
separation of per-tool build identities.

GitHub rejected additions to the huge original Linux workflow YAML
before jobs could start, even minimal inline opt-in conditionals.
The **accepted original workflow YAML has been restored**.
The RV32 opt-in is now deliberately read from the explicit commit
marker inside `tools/ci/linux-runtime-gcc-cohort-bisect-v1.sh`,
while native link dispatch is in
`tools/ci/linux-runtime-native-union-v1.sh`.
The dedicated real-runtime ABI verdict is in
`tools/ci/qualify-minild-vdso32-real-kbuild-v1.sh`.
This reuses the existing seven producer jobs and existing QEMU receiver
without new workflows.

## Remaining scope, without false completion

1. **RV32 compat-vDSO ELF32 native input-to-output linking** has now
   passed the four genuine Linux object isolation gate; **real Kbuild,
   full QEMU P1 and zero GNU product fallback require the separate
   same-source full integration gate**. Do not treat isolated linkage as
   Linux runtime acceptance until that exact CI succeeds.
2. The existing link-feature probes may remain GNU without preventing
   an otherwise native product (they are not product links).
3. C-level phase instrumentation was deliberately **not** promoted over
   an accepted compiler source hash. Repeat full 2064+QEMU certification
   after any future production compiler-source change.
4. The other four preserved branch refs are ancestors of `main`
   (`behind_by=0` for each when compared to `main`). Their code is
   not missing from the canonical branch; branch-ref deletion is
   separate repository housekeeping, not a missing merge.
