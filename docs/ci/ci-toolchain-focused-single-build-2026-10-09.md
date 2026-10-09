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
