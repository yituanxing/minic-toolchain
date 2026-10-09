# Linux runtime fault isolation

This document defines the canonical method for isolating MiniC-generated Linux
runtime failures.  Repository cleanup must preserve runtime evidence first; it
must not make a failing gate easier to pass simply to reduce CI noise.

## Cleanup baseline

The cleanup started from development branch
`agent/linux-expanded-kbuild-v0` at `c51638e03be3ac8b133fd649780c21db06657fd8`
(`ci: capture early FDT through physical breakpoints`).  While this document was
being prepared the live diagnostic branch advanced normally to
`48cb5895629cb3b47f953b9ebc1191152bf8ca8e`
(`ci: trace early RISC-V boot breadcrumbs`).  That concurrent progress is kept:
cleanup commits must be fast-forward additions and must never force-update the
branch.

The Linux runtime campaign targets Linux `6.6.143`.

## Why this method exists

The earlier Python compiler campaign already demonstrated a reliable isolation
strategy.  Starting from a known-good GCC Linux image, each trial restored the
GCC object set and overlaid only a controlled MiniC candidate set.  Ordered
prefix testing once reduced a failure from `prefix91 = PASS` and
`prefix92 = FAIL` to the single owner `kernel/sched/core.o`.

The important result was not that particular object.  The important result was
that every trial began from the same baseline, so stale objects from an earlier
trial could not create a false PASS or false FAIL.

## Required invariants

Every runtime-isolation trial must:

- start from one certified Linux/GCC baseline;
- restore pristine GCC objects before applying a MiniC candidate overlay;
- overlay exactly the requested candidate set;
- record object paths and verify object identity/digests where practical;
- use the same link inputs, kernel command line and QEMU configuration;
- classify the result as `PASS`, `FAIL` or `INCONCLUSIVE`;
- never interpret timeout alone as a pass;
- preserve serial/GDB/frontier evidence sufficient to inspect the verdict;
- keep fixture/cache/link/harness failures separate from compiler runtime
  failures.

A compiler fix is not complete until it has passed the single-object
reproducer, the relevant owner cohort, and the full Linux image.

## Canonical pipeline

```text
certified GCC Linux baseline
          |
          v
restore pristine GCC objects
          |
          v
overlay candidate MiniC object set
          |
          v
verify candidate manifest/digests
          |
          v
relink -> QEMU -> frontier classifier
          |
   +------+------+----------------+
   |             |                |
 PASS           FAIL          INCONCLUSIVE
   |             |                |
   |             v                `-> repair harness/evidence first
   |     ordered prefix/subset bisection
   |             |
   |             v
   |        single object owner
   |             |
   |             v
   |         GCC-object swap
   |             |
   |             v
   |      MiniC feature switches
   |             |
   |             v
   |       minimal code pattern
   |             |
   `-------------+-> compiler fix
                         |
                         v
             single -> cohort -> full Linux
```

If two individually passing objects fail only in combination, stop assuming a
single owner and reduce the interacting set explicitly.

## Existing primitives to reuse

New workflows should compose existing tools instead of embedding another copy
of the same mechanics.  Current useful primitives include:

- `tools/ci/linux-runtime-frontier-v1.py` — runtime progress/verdict
  classification;
- `tools/ci/linux-runtime-qemu-watch-v1.py` — QEMU/runtime observation;
- `tools/ci/linux-first-fault-analyzer-v1.py` — first-fault analysis;
- `tools/ci/linux-early-runtime-link-v1.sh`, `v2.sh`, `v3.sh` — early-runtime
  relink generations;
- `tools/ci/linux-fast-relink-shadow-v1.sh` — fast shadow/relink support.

Existing fixture producers, source caches and MiniC runtime-profile builders
remain authoritative until an equivalent canonical replacement is verified.

## Current FDT experiment

`.github/workflows/linux-runtime-fdt-isolation-v1.yml` is a live diagnostic and
must not be archived while it is producing evidence.  At
`48cb5895629cb3b47f953b9ebc1191152bf8ca8e` it deliberately uses two parts of
the canonical isolation method:

1. replace the linked `lib/fdt_ro.o` owner with a GCC-built object;
2. trace physical-address early-boot breadcrumbs through `_start`,
   `_start_kernel`, `setup_vm`, `set_satp_mode`, page-table setup,
   `create_fdt_early_page_table`, MMU relocation and finally `start_kernel` or
   the first `die()`.

This experiment should eventually contribute reusable mechanics to the common
harness; the one-off YAML itself should not become a permanent runtime gate.

## Known separate harness/link issue

The generated-kallsyms diagnostic previously rebuilt the exact MiniC runtime
profile and selected runtime owners successfully, but stopped before QEMU while
relinking generated kallsyms data.  The linker reported a RISC-V floating-point
ABI merge mismatch (`double-float` versus `soft-float`).  Until execution proves
otherwise, classify that as a generated-object/link-harness problem rather than
as a new MiniC runtime code-generation fault.

Last verified fixture identities from that campaign are recorded here for
provenance and must be revalidated before being treated as current truth:

- full linked fixture:
  `linux-expanded-mixed-v1-6.6.143-Linux-d30569a4c675f6ac72d9f63c28f0b8745317f7a15c0799d10768f916d74b3a88`;
- Linux source:
  `linux-source-6.6.143-dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932-v1`;
- frozen linker subset:
  `linux-runtime-frozen-v1-6.6.143-e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7`.

The associated runtime profile checked:

- `MINIC_INLINE_ASM_LOCAL_INTEGER_FACTS_V0=APPLIED`;
- `MINIC_INLINE_ASM_RK_CAST_TAIL_V0=APPLIED`;
- `MINIC_RISCV64_PI_LOCAL_SYMBOL_ADDRESS_V0=APPLIED`.

## Workflow lifecycle

Workflows are classified as one of four kinds:

1. **build/runtime primitives** in `tools/ci/`;
2. **canonical gates** that should remain permanently registered;
3. **live diagnostic experiments** for one unresolved fault;
4. **historical diagnostics** retained for provenance but moved outside
   `.github/workflows/` after equivalence is proven.

Do not archive a live workflow merely to make the Actions page cleaner.  First
move its reusable behavior into a primitive/canonical gate, reproduce the old
verdict from the same inputs, and only then archive the old YAML.

## Cleanup acceptance gate

Before consolidating or deactivating a runtime workflow, compare old and new
paths using the same:

1. compiler/source SHA and Linux version;
2. certified fixture and candidate-object manifest;
3. relink inputs and QEMU command line;
4. `PASS` / `FAIL` / `INCONCLUSIVE` classification;
5. highest runtime frontier and first-fault evidence where applicable.

Cleanup passes only when the canonical path reproduces the old verdict, or
moves the frontier later with inspectable evidence.  A cache miss, missing log,
link failure, or timeout without progress evidence is not a runtime pass.


## GNU-locked single-TU debugging baseline (2026-10-09)

The first debug campaign fixes **all non-MiniC stages to GNU** rather than
changing the whole Mini toolchain at once:

```text
Linux .c + pinned Kbuild flags + pinned GCC builtin environment
   -> GNU GCC -E -P (ONCE, immutable .i)
   -> { GCC -S from the same .i | MiniC -S from the same .i }
   -> same GNU assembler / RISC-V LP64 ABI
   -> { GNU .o | MiniC .o } -> verified object overlay
   -> GNU ar / ld / objcopy -> same QEMU and runtime oracle
```

**Two different GCC references must not be confused.** The existing
`linux-runtime-gcc-baseline.yml` builds a full GCC kernel after enabling extra
initramfs/serial options; the older `linux-expanded-mixed-v1` runtime cache
has a pinned `.config` SHA256
`e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7`
and is already mixed MiniC/GCC. Neither reference is automatically a
**pure-GCC, exact-config** golden object pool compatible with the other.
Before *any* whole-kernel GCC/MiniC object bisect, materialize and boot-check
one pure-GCC pool at **exactly the same** Linux version, `.config`, input
generation, compilation options, ABI and linker context as the candidate.
If this gate is missing, do not claim the result isolates MiniC.

**Single-object fast loop:**
1. Materialize a chosen Linux source's exact Kbuild `.i` once with GNU GCC,
   retain bytes/SHA256, flag vector, predefines, .config hash and compiler IDs.
   MiniPP exact 3352/3352 is useful independent evidence, but do not change
   preprocessors during this phase.
2. For the first A/B trial only, feed that SAME `.i` to GCC's *C compiler*
   (`-x cpp-output -S`) and to MiniC (`-S`); pass both resulting `.s`
   through the GNU assembler using the exact recorded RISC-V `-march/-mabi`
   and Kbuild-relevant flags. Verify ELF machine, ABI and object identity.
3. Cache the immutable GCC `.i` and GNU `.o`. After a MiniC source fix,
   rebuild MiniC and **only MiniC's** changed `.s/.o` from frozen `.i`;
   do not re-run GCC preprocessing or GCC code generation when their
   inputs/config/options are unchanged.
4. Restore the complete certified **pure-GCC** object pool on every trial,
   overlay only the explicitly selected MiniC candidate object(s), authenticate
   object IDs, and GNU-relink. Linux link inputs, archives and generated
   kallsyms remain under the pinned fixture contract. QEMU must be classified
   PASS/FAIL/INCONCLUSIVE with first-fault/progress evidence.
5. Start from one-file candidates and historical fault pools, use ordered-prefix
   bisect and the existing `single` / `all-except` object modes. A prefix
   boundary is not automatically a standalone culprit. Only investigate
   multi-file interaction if normal single-file experiments cannot explain
   the failure.
6. After each real compiler fix, recheck that object's reproducer, related
   objects, then the full Linux Image/QEMU. A fast early frontier movement is
   not the same as a complete boot PASS.

**Performance and identity constraints:** The one-time pure-GCC preparation
and whole-kernel certification are deliberately separate from the fast loop.
Record separately MiniC rebuild, changed-TU compile, object reset/relink and
QEMU timing. Avoid new active workflow YAMLs: drive these stages from an
existing canonical Runtime owner once the equivalent object/link/QEMU
provenance is demonstrated. Existing `stage2_kbuild_cc.sh` already uses GNU
GCC -E -P, MiniC -S and the GNU assembler, so this is an isolation of *current
working stages*, not a speculative toolchain swap.


**Implemented first-stage primitive (not yet an end-to-end QEMU gate):**
`tools/ci/linux-runtime-gcc-minic-ab-v1.py` provides `establish`
(GNU-from-.i reference and MiniC-from-the-same-.i candidate) and `iterate`
(pin-verification, MiniC-only rebuild). It requires literal GNU compiler
and assembler flags files extracted from the exact Kbuild command; it does
not silently guess kernel ABI flags. Outputs include `gnu/reference.o`,
`minic/candidate.o`, `baseline.json` and `last-experiment.json`.
The candidate build status is `OBJECTS_BUILT_NOT_RUNTIME_CERTIFIED`.
Offline contract tests:
`python3 tools/ci/test_linux_runtime_gcc_minic_ab_v1.py`.
A real RISC-V TU replay, full same-config GCC baseline, GNU-relink and
QEMU oracle are **separate, not-yet-performed certification steps**.


## 2026-10-09 pure-GNU golden checkpoint and first single-object consumer

Real [GCC golden run 37934420543](https://github.com/yituanxing/minic-toolchain/actions/runs/37934420543)
completed the original full user-space/P1 QEMU contract, enumerated
**2141 RISC-V ET_REL objects** (excluding native HOSTCC products), and
successfully saved the authenticated output tree + manifest in the
Runtime branch-scoped GitHub Actions cache. Golden \`.config\` SHA256 is
\`eeb04f304ebfb97a1079c187dc0c2ea3cc61fc50a55aa3cc776237c16ab19bbe\`;
this differs from the historical mixed-cache \`e538a6...\` and is a
**new, independently boot-certified build universe**.

The initial candidate TU is \`lib/idr.o\`. The existing GCC baseline workflow
now has a separate opt-in **gcc-single** job activated by the exact commit tag
\`[linux-runtime-gcc-single]\`. It restores the certified GCC objects from
the cache, restores the pinned Linux sources at the original GNU Kbuild
source-path spelling, builds MiniC, then runs
\`tools/ci/linux-runtime-gcc-single-swap-v1.sh\`.
The script saves the reference \`.o/.cmd\`, forces only \`lib/idr.o\`
through the existing GCC -E / MiniC -S / GNU-as wrapper, preserves the
resulting \`.i/.s/.o\`, restores the exact original GNU \`.cmd\`, and invokes
normal GNU Kbuild incremental \`make Image\`. It rejects a GNU rebuild that
overwrites MiniC's selected object, rejects an unchanged Image, and then runs
the existing full user-space QEMU oracle. It restores the GCC object and
command metadata on exit and always uploads diagnosis logs. No permanent
new workflow YAML was created; the opt-in job is skipped on normal pushes.

**Verification boundaries:** the existing \`gnu-locked-single-tu\` primitive
has offline mock tests, the new single-object swap script has a shell syntax
gate, and the golden GCC build/QEMU/cache are real green evidence. The
one-object swapped Image and QEMU must get their own **real CI run** before
claiming any MiniC runtime PASS. Once this first run is stable, freeze
its \`.i\` and exact assembler flags under source/config/GCC/Kbuild-command
identities so subsequent MiniC-only iterations avoid even repeating GCC -E.


### First real one-owner QEMU proof and immutable GNU input reuse

[Run 37935628658](https://github.com/yituanxing/minic-toolchain/actions/runs/37935628658)
was **SUCCESS** on \`lib/idr.o\`: restored the 2141-object GCC pool,
built exactly the chosen MiniC object with the existing GCC-PP/MiniC/GNU-AS
Kbuild wrapper, GNU-relinked Image and passed the full existing runtime
initramfs + shell QEMU oracle. Logs show a distinct MiniC object SHA and
changed final Image SHA. This is a **hybrid single-object** certificate,
not a full-MiniC Linux image boot.

Next opt-in runs cache the exact \`lib/idr.i\` + GNU assembler flags
under SHA256(config, original GCC \`.o\`, original GCC \`.cmd\`,
source file, GCC executable). On a matching cache hit the runner
**skips even GNU GCC -E and single-object Kbuild**: MiniC -S ->
GNU assembler -> GNU relink -> QEMU. Any incomplete/altered or
identity-mismatched cache fails closed. Performance timing of this
optimization must be measured separately from the original real run.


### Real immutable .i replay and hot-loop timing (2026-10-09)

- [GNU one-object Kbuild capture / cache producer #37936412382](https://github.com/yituanxing/minic-toolchain/actions/runs/37936412382):
  \`GNU_SINGLE_PP_CACHE=MISS\`, real \`lib/idr.i\` captured from the
  source/config/GCC/\`.cmd\`-pinned GNU stage; MiniC object + GNU incremental
  Image + QEMU all **PASS**; exact input/assembler-flag cache **saved**.
- [MiniC-only frozen-input replay #37936800398](https://github.com/yituanxing/minic-toolchain/actions/runs/37936800398):
  independent runner restored both the golden GCC pool and exact \`.i\` cache;
  \`GNU_SINGLE_PP_CACHE=HIT\`, \`GNU_SINGLE_MINIC=PASS\`,
  \`GNU_SINGLE_RELINK=PASS\`, \`GNU_SINGLE_RUNTIME=PASS\`.
  Hot-loop instrumented durations: **compile 933 ms**, **GNU relink
  18,698 ms**, **initramfs/QEMU runtime 12,861 ms**, **total 32,493 ms**.
  These are **within-job trial timings** after the compiler/fixture/cache
  setup, not total GitHub Actions runner wall time. The producer miss spent
  about 18 s from GNU object verification to MiniC result; timings are not
  strictly paired/hardware-identical benchmark observations.
- Runtime [M0 #37936800311](https://github.com/yituanxing/minic-toolchain/actions/runs/37936800311)
  also passed on the same HEAD; the extra T2 paths were routed to skipped
  expensive jobs, not unrequested 3352 rebuilds.

This proves the sought **frozen GNU PP -> MiniC-only \`.i\` replay -> GNU
as/link -> real QEMU** loop for \`lib/idr.o\` on the 2141-object GCC kernel.
It neither replaces nor certifies an entirely MiniC-built Linux Image.
Continue with single-file failures, controlled object overlays, and
regression expansion; multi-object interaction is deferred unless a
single-object explanation becomes impossible.


## 2026-10-09 first GNU golden single-object census — 14 actual QEMU passes

Single-object trials on the **same** booted GCC Linux 6.6.143 golden kernel:
\`config_sha256=eeb04f304ebfb97a1079c187dc0c2ea3cc61fc50a55aa3cc776237c16ab19bbe\`.
Every trial restored the golden object pool independently, rebuilt just its
MiniC candidate, GNU-relinked a **changed** Linux Image and passed the
full initramfs/shell QEMU oracle. These are not generic all-MiniC or joint
multi-object certificates.

| Workflow run | Individually swapped MiniC object(s) | Actual outcome |
|---|---|---|
| [37936800398](https://github.com/yituanxing/minic-toolchain/actions/runs/37936800398) | \`lib/idr.o\` | full QEMU PASS, frozen input cache HIT |
| [37939800272](https://github.com/yituanxing/minic-toolchain/actions/runs/37939800272) | \`kernel/sched/core.o\` | full QEMU PASS, input captured |
| [37940288951](https://github.com/yituanxing/minic-toolchain/actions/runs/37940288951) | \`lib/xarray.o\`, \`kernel/fork.o\`, \`mm/memory.o\`, \`kernel/locking/spinlock.o\` | 4/4 independent QEMU PASS |
| [37940760024](https://github.com/yituanxing/minic-toolchain/actions/runs/37940760024) | \`arch/riscv/mm/init.o\`, \`arch/riscv/kernel/setup.o\`, \`arch/riscv/kernel/irq.o\`, \`kernel/time/timekeeping.o\` | 4/4 independent QEMU PASS |
| [37941213469](https://github.com/yituanxing/minic-toolchain/actions/runs/37941213469) | \`lib/string.o\`, \`mm/page_alloc.o\`, \`kernel/rcu/tree.o\`, \`arch/riscv/kernel/process.o\` | 4/4 independent QEMU PASS |

Every batch retained the canonical \`linux-runtime-gcc-baseline.yml\`
opt-in \`gcc-single\` job using a deliberately small four-element matrix:
each one independently restored the pinned original GCC pool, so a
previous experiment did not contaminate the next. Runtime M0 on the
latest batch also [passed](https://github.com/yituanxing/minic-toolchain/actions/runs/37941213205).
No new active workflow file was created. Every target's frozen GNU \`.i\`
and assembler flags have distinct cache identities and were prepared for
fast future MiniC-only replay.

**Crucial interpretation:** a historical prefix boundary around
\`kernel/sched/core.o\` on the **older mixed-config** kernel is not
reproduced by the \`core.o\`-only experiment on this **new GCC golden
config**. This does *not* prove the original fault has been fixed.
Likewise, 14 independently passing single-object tests do *not* prove
that 14 MiniC objects work together or that an all-MiniC Image boots.

**Next useful gate:** obtain a **verified FAIL** for a sufficiently broad
MiniC cohort or full MiniC kernel using exactly this golden \`.config\`,
GNU preprocessing/assembly/link and the same QEMU oracle. Without that,
continued successful one-file probes are coverage but cannot locate a
runtime culprit. Once a same-universe FAIL exists, isolate with GNU-object
reset, prefix bisection and confirmation by one-file swap / GCC exclusion;
defer complex interactions until ordinary single-owner diagnosis fails.
