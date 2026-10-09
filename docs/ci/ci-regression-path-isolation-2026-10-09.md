# CI logic audit: regression push-path isolation — 2026-10-09

A live push modifying **only** `tools/ci/runtime-timekeeping-trigger.txt` (run head `7a991823fcc2a79fe5a25626ae29cd429630056e`) proved the repaired Runtime Owner Focused selector schedules exactly two jobs: `route`, then **`owner (timekeeping)`**, without scheduling `vsyscall`, `notifier` or `build-policy`. However, the same push triggered three *unrelated* and expensive automatically-running suites:

- `MiniC RV64 Focused Regressions V1` run [#37879421098](https://github.com/yituanxing/minic-toolchain/actions/runs/37879421098).
- `MiniAR Regressions V1` run [#37879421208](https://github.com/yituanxing/minic-toolchain/actions/runs/37879421208).
- `MiniLD A0-A6 Regressions V1` run [#37879421090](https://github.com/yituanxing/minic-toolchain/actions/runs/37879421090).

All three were triggered by their broad `on.push.paths: tools/**`, not by a code change affecting these toolchains. This is avoidable CI runner waste, even though each job's real test is valid.

## Corrected push routing

The three regression workflow sources now have **ordered GitHub Actions negative path globs** *after* all historical positive globs:

`!tools/ci/runtime-*-trigger.txt`

`!tools/ci/linux-runtime-rest-init-frontier-v0.json`

`!tools/ci/select_runtime_owner_modes_v1.py`

GitHub evaluates push path patterns per changed file. A push consisting solely of Runtime sentinel/selector changes should no longer start MiniC/MiniAR/MiniLD regressions; a push containing `src/**`, `archiver/**`, ordinary `tools/**`, `tests/**` or a change to the workflow's own YAML **still triggers** the full relevant regression. No job code, source, shell command, archive/ELF assertion or artifact handling is changed.

The Runtime branch originally had broad positive filters; those were retained byte-for-byte and only the three negatives were appended. The Performance branch historically had **no push path filter** and two old Runtime/cleanup branch names; it now uses the same positive source scopes and three negatives while retaining the original branch list and workflow manual dispatch. This deliberately prevents unrelated documentation-only push runs on the obsolete cleanup branch too; it does **not** turn on heavyweight regression pushes on the Performance development branch.

`tools/ci/check_ci_regression_path_isolation_v1.py` is in both M0 gates. It restores the previous complete YAML and compares its exact Git blob SHA for all six historical files; checks negative ordering, unrelated Runtime-only changes being excluded, genuine code/test/script changes included, and mixed pushes not being masked by negative matches. M0 is static proof, not a fresh compile regression PASS.

## Outstanding

- Perform a **second live push** of a dedicated Runtime sentinel after this fix and observe the three irrelevant workflows are not triggered; before/after run IDs must be recorded.
- Existing Runtime QEMU `RCU` watcher reached `console-init` but returned `INCONCLUSIVE` at its second stage. No Linux runtime correctness/boot certificate follows from successful routing.
- Review other expensive workflows triggered by broad paths and branch compatibility before merging more YAMLs. The current target is correct dispatch, not reducing the 30/31 active-entrypoint counts.
