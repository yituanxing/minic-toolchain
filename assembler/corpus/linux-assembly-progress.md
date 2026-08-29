# MiniAS Linux Assembly Progress Ledger

This ledger is the persistent human-readable index for Linux assembler
convergence. Workflow artifacts and exact GitHub Actions runs remain the evidence
source; this file records the promotion state and makes the update rules explicit.

## Frozen build identity

- Linux: 6.6.143
- Architecture/config: RISC-V `defconfig`
- C translation units selected by the configured Kbuild plan: **3352**
- Configured native preprocessed assembly objects sourced from `.S`: **26**
- Configured raw `.s` objects: **0**
- Configured Rust objects: **0**
- Total configured assembler-stage object inputs: **3378**
- Input-inventory run: `33237625114`
- Inventory head: `d0601327e2a32e38cbaafb094a119166768686e7`

The frozen compiler corpus and the native assembly cohort are separate coverage
populations. The original configured-plan inventory counted 3378 assembler-stage
inputs, while the later successful ground-truth Kbuild inventory measured **3536**
actual assembler inputs: 3501 C compile rules + 35 native `.S` rules, with 0
raw `.s` and 0 Rust rules (run `33237934886`). The frozen 3352 lane remains
the convergence oracle first; full Linux MiniAS acceptance must eventually be
reconciled against the 3536 ground-truth workload.

## Window ledger

| Population | Frozen range | Exact head | Run | PASS | FAIL | ERROR | Wall time | State |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| C first500 | 0-499 | `c7bad59bb975a6cc66db6430fd5e0764b4c77add` | `33237394302` | 500 | 0 | 0 | 149 s | **FROZEN** |
| C new500 | 500-999 | `d0601327e2a32e38cbaafb094a119166768686e7` | `33237625079` | 500 | 0 | 0 | 222 s | **PASS / freeze next** |
| C next500 | 1000-1499 | `c781ab134adedf1dff32efd8c2652d5b9bcafd23` | `33237834301` | 500 | 0 | 0 | 139 s | **PASS** |
| C next500b | 1500-1999 | `787fc6a888b7c9c1ead201f28c64295d7dd2fb56` | `33238043396` | 500 | 0 | 0 | 190 s | **PASS** |
| C next500c | 2000-2499 | `ba87fd14d9b19e3eb65d596e71a0b949267176b9` | `33238335068` | 500 | 0 | 0 | 201 s | **PASS** |
| C next500d | 2500-2999 | `af52735e7df06e9cccb106d6dba485a596bb28a5` | `33244118305` | 500 | 0 | 0 | 187 s | **FROZEN** |
| C final352 | 3000-3351 | - | - | - | - | - | - | **active** |
| native asm26 | configured .S objects | - | - | - | - | - | - | active |

Cumulative authoritative frozen-C coverage now proven: **3000/3352**. All six
500-file windows are exact green. Run `33244118305` closed the only next500d
failure (`drivers/perf/riscv_pmu.i.s`, previously `bad-csrr`) by accepting
the additive constant CSR expression emitted from the real Linux inline-asm
shape. The remaining frozen-C frontier is exactly **352 files** (3000-3351).

## First500 convergence history

| Exact head | Run | PASS | FAIL | ΔPASS | Wall | First-blocker state after run |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `2ff12330d6d0b1332f7b9f5e69c1b69731a9f21a` | `33235579467` | 483 | 17 | baseline | 167 s | ecall×5, csrrw×3, .set×3, .insn×3, move×2, .incbin×1 |
| `f126db6567119dbcdbcceffd266aa25215d67c5b` | `33235778345` | 488 | 12 | +5 | 168 s | csrrw×3, .set×3, .insn×3, move×2, .incbin×1 |
| `8c7485a598f398b93a172630b3d1532b03ff18a0` | `33236277040` | 490 | 10 | +2 | 167 s | .insn×4, .set×3, move×2, .incbin×1; one csrrw TU advanced to .insn |
| `8eeecfe1645bf48e84ff7c5c9682bb91abe26e7a` | `33236778048` | 493 | 7 | +3 | 178 s | .set×3, move×2, bad-register:0×1, .incbin×1; one .insn TU advanced rather than passing |
| `1b9727c0612f34e59e186d820c6b7c19ce6c53f3` | `33237228092` | 499 | 1 | +6 | 163 s | .incbin×1 |
| `c7bad59bb975a6cc66db6430fd5e0764b4c77add` | `33237394302` | 500 | 0 | +1 | 149 s | none |

This history intentionally distinguishes **coverage progress** from **frontier
advance**. A TU whose first blocker moves later remains FAIL until it emits a
valid RISC-V ELF relocatable object.

## Required update procedure

After every authoritative full-window run:

1. Record exact head, run ID, selected count, PASS, FAIL, ERROR and wall time.
2. Record `ΔPASS` relative to the previous full run for that same frozen window.
3. Persist the complete failure pool and first-blocker Pareto from the artifact.
4. Refresh the active sample to at most 16 current failures. If fewer than 16
   remain, shrink naturally (sample10/sample7/sample1).
5. A TU leaves the active sample only on true PASS. A later/changed blocker is
   recorded as **frontier advance** and the TU stays active.
6. Pull internal/error/crash failures into a dedicated ERROR cohort immediately;
   no window may freeze while ERROR is nonzero.
7. Use exact-shape micro tests plus coherent blocker cohorts for implementation,
   then replay the whole active sample.
8. Keep the slow full-window lane independent of the fast active-sample lane.
   Sample success never rewrites the full-window headline.
9. On exact `500/500` (or `352/352`), freeze the head/evidence and open the
   next fixed window immediately.
10. Keep already-frozen windows as cumulative regression contracts.
11. After all 3352 frozen C TUs converge, reconcile the additional C compile
    rules from the 3536 ground-truth inventory and require all 35 native `.S`
    rules as well.
12. Final MiniAS Linux acceptance requires an exact ground-truth aggregate gate;
    the current measured target is **3536 assembler inputs**.

## Timing policy

Record wall-clock time from each GitHub Actions run rather than extrapolating it
into evidence. Current observed full-window runs are roughly 2.5-4 minutes:
first500 most recently took 149 seconds and new500 took 222 seconds. The final
all3378 wall time must be measured by its own aggregate run; projected time is
not a substitute for that measurement.
