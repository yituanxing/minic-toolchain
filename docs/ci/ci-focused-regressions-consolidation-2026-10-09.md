# Three-to-one focused toolchain T1 consolidation — 2026-10-09

Source code, tests, build steps, timeouts and artifact-upload steps are **not changed**. Previously independent MiniC RV64, MiniAR and MiniLD auto-push workflows now share one YAML file: `.github/workflows/toolchain-focused-regressions-v1.yml`. The three **independent jobs remain distinct**, to prevent a linker failure masking an archive/compiler result.

| Archived original | New independent job | Runtime historic Git blob | Performance historic Git blob |
| --- | --- | --- | --- |
| minic-rv64-focused-regressions-v1.yml | minic-rv64 | `883f7d7cfea0dc1f66154955783e969e3391061d` | `b4df1d99eb1481f0df2d4e941a2d3024ef28815c` |
| miniar-regressions-v1.yml | miniar | `ac16bda61647a955353c2d580725273c6955c6fb` | `31844b55896702273ebc5efd08518d2de7f536d6` |
| minild-regressions-v1.yml | minild | `3b05489a952152f810c70afa6b43fa252ddf47d5` | `ec6e534cc4020442bdb58dcb64fc338276bd8641` |

Historical YAMLs live byte-for-byte under `.github/workflows-disabled/` in each branch; `tools/ci/check_ci_regression_path_isolation_v1.py` verifies all 3 original SHA values on each branch and compares every original executable job body (including artifacts and test commands) with the canonical job after removing only the new dispatch guard and job name.

The one new `workflow_dispatch` offers `all`, `minic-rv64`, `miniar` and `minild`. A source push matching the historical union of positive globs runs all three. Runtime-only sentinels and pure CI maintenance scripts remain excluded. No additional Linux Image or QEMU jobs are included. Old independent workflow cancellation groups are replaced by one canonical ref/mode group, so newer pushes cancel earlier redundant T1 runs as a unit.

Active YAML count changes: **30 → 28 Runtime**, **31 → 29 Performance**, **34 → 32 distinct names**, **61 → 57 branch-path YAMLs**. **Independent T1 job count stays 3 per branch**. Existing three separate `make all` rebuilds still overlap; their environment differences and independent verdicts should be audited before attempting to share runners or artifacts.

M0 proves structure and job-body equivalence only. A real automatic source-path push must still prove the combined route. Historical pre-convergence T1 runs are not evidence of a successful post-convergence run.
