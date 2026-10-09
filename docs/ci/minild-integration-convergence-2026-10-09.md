# MiniLD integration workflow convergence — 2026-10-09

## Scope
Three opt-in MiniLD integration workflows were consolidated into one maintained entrypoint `.github/workflows/minild-integration-v1.yml` on both development branches. The automatically triggered `minild-regressions-v1.yml` stays separate because it has path-scoped T1 checks with different trigger behavior.

## Former entrypoints and byte-identical archives

| Retired YAML | Original Git blob SHA | Maintained job ID | Former push tag |
| --- | --- | --- | --- |
| `minild-dynamic-integration-v1.yml` | `9f28d3712c9a5aaea26b220d3de006d98e9d064c` | `dynamic-integration` | `[minild-dynamic-integration]` |
| `minild-linux-rel-boundaries-v1.yml` | `826677e079536e2901b697cb81cc8caeb9e5f261` | `linux-rel-boundaries` | `[minild-linux-rel-boundaries]` |
| `minild-static-runtime-v1.yml` | `1b48c8a042d3ea2155fec45b40be6f48c375a40a` | `musl-lua-static` | `[static-runtime]` |

The original YAMLs are present verbatim under `.github/workflows-disabled/` and the active copies are retired. The preserved historical YAML blob SHA is verified by `tools/ci/check_minild_integration_convergence_v1.py`; the checker compares original vs. consolidated job bodies after removing only the dispatch predicate and moved concurrency keys. No build, assertions, GNU/GCC oracles, timeout, QEMU commands, or upload-artifact step bodies are changed.

## New invocation contract

- Manual `workflow_dispatch` mode: `dynamic` (default), `linux-rel`, `static`, or `all`.
- Exact existing three push tags retained. A single tagged push may request more than one integration lane if it contains multiple tags.
- Push is now eligible on both existing development branches, whereas the old three workflows listened only to the Runtime branch. Each job still requires the original explicit tag on pushes.
- Concurrency moved from *workflow scope* to *per-job scope*, preserving each original group name and `cancel-in-progress: true`, so an unrelated tagged integration job cannot cancel a distinct lane. This is an intentional cancellation-scope improvement.
- Dedicated always-push fast MiniLD `minild-regressions-v1.yml` remains untouched. The heavy integration lanes remain opt-in.

## Verification contract and remaining execution gate

- Source-level exact-job-body check: 3 of 3 preserved (except dispatch/concurrency semantics above).
- M0 runs Python archive/body/trigger checks and Ruby YAML parsing, not a real Linux or dynamic-link runtime test.
- Execute a true mode-specific integration run with current pinned compiler identity before marking T3/T4 certified; no such real run is claimed by this commit.
- Before further merges, inventory downstream consumers and confirm artifact names `minild-*` are stable.

## Counts
Expected active YAML inventory after successful two-branch migration: Runtime 51 -> 49, performance 50 -> 48, cross-branch unique names 55 -> 53. Original definition bytes recoverable via archived blobs and Git history.
