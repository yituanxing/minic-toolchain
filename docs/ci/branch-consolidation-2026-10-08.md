# One-shot branch consolidation / 2026-10-08
Precondition runtime branch `1e0d5cb4e9c9671eb1e3034d17be183169752874` and archival head `cc7a13bf7a1031ddb6c8fcd0a38ccac5d9bc8260`.

## Intent
Retain exactly four remote ref names: `main`, `agent/linux-expanded-kbuild-v0`, `agent/linux-perf-boolean-domain-v1` and `archive/all-progress-2026-10-04`.

The adjacent manifest pins **25 branch names to their exact 40-character tip SHA**. Each tip is verified to be reachable from the current archival octopus commit. Before deletion, the workflow rejects a changed remote tip, a current open PR head, or an in-progress CI run. Deletion uses atomic Git SHA leases, not unchecked `git push --delete`.

## Critical distinction
This operation removes obsolete *branch refs*, not history, patch files, scripts, CI evidence, workflow source, nor commits. The archive branch contains all 27 agent tips and later perf cleanup tip, with no active workflows in its worktree. The deleted ref names can be restored from `tools/ci/branch-consolidation-20261008.tsv`.

**This is not a source merge:** experimental changes are preserved as historical commits, but are not automatically copied into the production/runtime compiler source tree. Do not claim divergent experiments have been productized. The active performance/profile and runtime branches intentionally remain distinct.

## After verification
Retire the temporary workflow into `.github/workflows-disabled/` and update `docs/ci/current-workflow-map.md` with the final branch list. Do not add a standing GC workflow. Leave Linux fixture/QEMU and other certification workflows untouched.
