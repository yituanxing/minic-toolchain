# MiniC Linux 6.6.143 — reproducible timing ledger (2026-10-10)

Purpose: retain **real CI observations** for post-runtime compiler optimization. Do not compare wall time, summed job durations and compiler CPU work as though they were the same metric.

## Identity / frozen environment

- Runtime branch: `agent/linux-expanded-kbuild-v0`.
- Kernel: Linux 6.6.143, RISC-V 64-bit.
- Pure-GNU golden configuration SHA256: `eeb04f304ebfb97a1079c187dc0c2ea3cc61fc50a55aa3cc776237c16ab19bbe`.
- Exact candidate universe: 2,064 GCC-C translation-unit objects divided into 7 fixed shards (six × 295, one × 294); non-C and post-link derived ELF objects are separately audited.
- GNU GCC preprocesses; MiniC emits RISC-V assembly; GNU assembler/linker generates ELF/Image. Current runtime candidate profile is **not** the separate opt-in performance profile.
- Candidate object cache is addressed by compiler executable SHA256, GCC/assembler identity, config/Image/golden identity, and exact target list. A cache hit is not a new compile benchmark.

## Real seven-runner MiniC compilation

Source: [Actions #38030853231](https://github.com/yituanxing/minic-toolchain/actions/runs/38030853231), commit `92a313bd88b8e9d95a28a43494e096c88933b3ef`. All seven shards successfully compiled and authenticated, then merged all 2,064 object files.

| Shard | Objects | COHORT_FULL_COMPILE elapsed_ms |
| --- | ---: | ---: |
| 0 | 295 | 540849 |
| 1 | 295 | 465861 |
| 2 | 295 | 528877 |
| 3 | 295 | 358678 |
| 4 | 295 | 438748 |
| 5 | 295 | 537343 |
| 6 | 294 | 365296 |
| **Sum (not CPU time)** | **2064** | **3,235,652 ms = 53.93 min** |
| **Slowest shard, approx. parallel critical path** | | **540,849 ms = 9.01 min** |

This excludes runner provisioning, fixture restoration, compiler bootstrap, checks, artifact upload, final GNU relink and QEMU. Kbuild can rebuild some explicitly requested goals, so 2,064 unique outputs does not necessarily mean 2,064 compiler invocations.

The historical ~30-minute Python compiler full-build claim was recalled from an earlier experiment; it is **not a validated matched A/B** on identical Linux input, GNU tools, runner load or build mode. Do not infer a CPU-level speed ratio from it.

## Runtime relink / header-reconciliation observations

| Actions run / compiler commit | Stage | Wall time | Result |
| --- | --- | ---: | --- |
| [#38032780370](https://github.com/yituanxing/minic-toolchain/actions/runs/38032780370), `16ce106` | First GNU Image relink | 46.789 s | GNU Image built |
| same | Kbuild recompile 4 vDSO-offset-dependent MiniC C owners | 15.890 s | 4/4 route SHA verified; second link changed generated headers again |
| [#38033243388](https://github.com/yituanxing/minic-toolchain/actions/runs/38033243388), `cf358022` | First GNU Image relink | 39.797 s | GNU Image built |
| same | Direct GCC-E / MiniC-S / GNU-AS replay of 4 dependent C owners | 9.245 s | 4/4 verified, current vDSO generated header stable |
| same | Second GNU Image relink | 37.501 s | Generated header stable, stopped at derived-object contamination audit (`.pi.o`), before QEMU |

Interpretation: recompiling only the four affected C owners against the newly generated `include/generated/vdso-offsets.h` is both necessary and much cheaper than rebuilding 2,064 C inputs. The full candidate cache remains immutable.

## Telemetry contract for next trials

- `COHORT_TIMING trial=<trial> stage=gnu_first_link elapsed_ms=...`
- `COHORT_TIMING trial=<trial> stage=minic_header_reconcile elapsed_ms=... owners=...`
- `COHORT_TIMING trial=<trial> stage=gnu_reconciled_link elapsed_ms=...`
- `COHORT_TIMING trial=<trial> stage=qemu_<profile> elapsed_ms=... rc=...`
- On actual shard cache **misses**, `COHORT_STAGE_TIME stage=gcc_E|minic|gnu_as ...` records count, summed subprocess wall time, P50, P95, max and slowest input.
- The existing receiver's `cohort.log` and `identity.txt` artifact contain the link/runtime timing measurements. The cache-hit path must say `COHORT_COMPILE=SKIPPED`, not report a fake zero-second compile benchmark.
- Keep build correctness, MiniC provenance, ABI validity, generated-header stability, and real QEMU verdict as separate gates. A successful GNU link alone is **not** a kernel boot pass.

Future matched performance comparison: fix MiniC SHA/config/input list and runner class, collect separate GNU-E, MiniC-S, GNU-as, link and QEMU timings on both baseline and candidate, then optimize the profiled hot path while keeping runtime tests green.
