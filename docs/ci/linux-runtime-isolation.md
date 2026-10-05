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
