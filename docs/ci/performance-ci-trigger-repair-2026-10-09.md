# Performance CI trigger repair — 2026-10-09

## Observed problem

Pure MiniLD CI-convergence commit `b844322310b274e32a6f030cbb09eb9a08ef9915` unexpectedly started expensive [First500 A/B 37864867479](https://github.com/yituanxing/minic-toolchain/actions/runs/37864867479), because `linux-optimized-first500-verify-v1.yml` matched `tools/ci/**` on any push and had no job-level opt-in predicate.

The same commit triggered [performance M0 37864867480](https://github.com/yituanxing/minic-toolchain/actions/runs/37864867480). Its **MiniLD consolidation checks passed**, but the newly enabled push gate failed later at `check-static-functions`; the cause of that compiler regression has not yet been classified. Do not relabel this failed whole run a PASS.

## Applied trigger policy

- `linux-optimized-first500-verify-v1.yml` remains fully manually dispatchable; a push now executes `perf500` only with explicit commit tag `[perf-first500]`. Narrow `tools/ci/**` to relevant MiniC profile and optimization helpers, preserving source, tests and explicit workflow paths.
- Performance M0 ordinary push is **T0 structure**, not implicit `make clean` and focused compilation. Those four historical full compiler boundary steps remain available via `workflow_dispatch` with `mode: full` (the default manual mode); `mode: structure` runs just T0.
- Added a cheap M0 assertion that an untagged helper-script edit cannot run First500 A/B.
- No compiler source code or performance test body changed. This repair prevents unrelated CI maintenance pushes from running expensive comparisons.

## Outstanding

- Investigate `check-static-functions` failure on the performance profile with an explicit T1/full run. The failed run remains the historical evidence.
- A changed workflow and a green M0 gate do **not** prove the First500 timing result.
