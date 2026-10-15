# True single-build T1 consolidation (MiniC RV64 / MiniAR / MiniLD) — 2026-10-09

## Motivation and previous bottleneck

The previous single YAML still launched **three independent Ubuntu jobs**, each performing a fresh apt update/install and `make -j4 MODE=release CFLAGS=-Werror ... all` into a different `BUILD_DIR`. A normal source push rebuilt nine-tool Mini toolchain three times for different RV64, archive/NM, and linker correctness checks. Prior real tests showed three approximately 25–35s independent jobs. Parallelism improved wall time but consumed three runners and duplicated installation/build work.

## Changed implementation

`.github/workflows/toolchain-focused-regressions-v1.yml` now has **one** `focused-t1` job and a shared release/Werror `make all` at `build/toolchain-focused-t1-v2`. The single job installs the *union* of prior oracle prerequisites, runs all eight preserved test-command blocks from the archived three-job YAML, and emits an eight-row per-test manifest. The three logical owners and manual dispatch choices `all`, `minic-rv64`, `miniar`, `minild` are unchanged.

| Owner | Original test blocks | Gate |
| --- | --- | --- |
| MiniC RV64 | 1 block: inline emission, constant-if link, section GC | `rv64` |
| MiniAR / MiniNM | 3 blocks: archive A0/A1, shared ELF reader, MiniNM A0/A1 | `ar_archive; ar_reader; ar_nm` |
| MiniLD | 4 blocks: shared ELF writer/reader/scripts, static A0–A3, dynamic A4–A6, section GC | `ld_shared; ld_static; ld_dynamic; ld_gc` |

Each test step uses `continue-on-error: true`, but the terminal **mandatory verdict step reads `steps.<id>.outcome`**, not the suppressed success conclusion. It marks overall job **FAIL** if *any* selected test fails, or if an unselected owner unexpectedly executes. This retains independent provenance in the `toolchain-focused-t1-suite-verdict` artifact and does not permit a false green partial run. The original RV64 evidence artifact name remains available.

### Historical source and regression guard

The previous three-job canonical workflow is preserved as `.github/workflows-disabled/toolchain-focused-regressions-three-jobs-2026-10-09.yml` with exact SHA `2b5e3809fe9fb8afc31aad988bd11333c8122901`. The original **three** pre-convergence workflow YAMLs remain separately archived (different Git blobs by Runtime/Performance branch). `tools/ci/check_ci_regression_path_isolation_v1.py` now checks all four archived sources' immutable SHA identity, the eight original executable test blocks character-for-character after reversing **only the BUILD_DIR substitution and added outcome guard**, the explicit three-owner manual modes, both branch auto-push scopes, and the presence of a single `make all`.

**Trust level**: static source proof is T0. A *real* source change that invokes the new T1 job must succeed on both development branches before describing this as successful regression operation. A failure is not automatically attributable to MiniC/AR/LD; first inspect the shared-build change and `steps.<id>.outcome`.

## Counts and trade-offs

The change **does not reduce active Workflow YAML count**: still Runtime **25**, Performance **23**, unique names **26**, 48 branch-path YAMLs. It removes **two physical jobs per branch**: Runtime 89→87, Performance 86→84, combined declared jobs 175→171. Three separate `make all` builds become **one** per canonical T1 push. The eight test blocks execute sequentially in one runner; the job may have somewhat longer wall time than the previous three parallel runners. The exact billed-minute and wall-time trade-off must be measured from real Action runs; do not report savings solely from job counts.

No Linux kernel full Image, 3352 TU, QEMU boot or Runtime frontier claim derives from this T1 refactor.

## Verified real two-branch automatic T1 executions

The source change modifying the canonical T1 entrypoint automatically ran the new **single physical job** on each development branch. Both executions completed **SUCCESS**. GitHub job logs printed `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`, followed by **eight explicit `success` rows** (RV64 1; MiniAR 3; MiniLD 4). The verdict manifest artifact was uploaded. These are actual T1 correctness/regression results; the M0 audit is a separate static source-equivalence proof.

| Branch | Prior 3-job run | Prior summed actual runner time | New 1-job run | New runner time |
| --- | --- | ---: | --- | ---: |
| Runtime | [#37892394268](https://github.com/yituanxing/minic-toolchain/actions/runs/37892394268) | 31+28+44 = **103 s** | [#37893508704](https://github.com/yituanxing/minic-toolchain/actions/runs/37893508704) | **51 s** |
| Performance | [#37892406875](https://github.com/yituanxing/minic-toolchain/actions/runs/37892406875) | 32+61+25 = **118 s** | [#37893522105](https://github.com/yituanxing/minic-toolchain/actions/runs/37893522105) | **50 s** |

This is a measured **52s Runtime / 68s Performance fewer total runner-seconds** in these particular comparable real runs. Prior 3 jobs overlapped in wall time, while new 8 checks are sequential, so **wall time can increase even if runner consumption falls**. Job start/end timings vary with external apt/cache/runner load. The paired runs are practical operational evidence, not controlled scientific performance benchmarks.

Both initial merged-head M0 runs also passed: [Runtime #37893508681](https://github.com/yituanxing/minic-toolchain/actions/runs/37893508681) and [Performance #37893522224](https://github.com/yituanxing/minic-toolchain/actions/runs/37893522224), checking 8 test script SHA-equivalent bodies against pinned archival sources and exported **87 Runtime / 84 Performance declared jobs**.

At this stage the `all` push route is proven on both branches; the three individual `workflow_dispatch` owner modes have structural M0 coverage but were not independently manually re-run.
