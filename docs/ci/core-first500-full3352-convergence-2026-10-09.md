# Linux Core First500 and Full3352 workflow convergence — 2026-10-09

The single maintained `.github/workflows/linux-core-all3352.yml` workflow now has **eight independent job definitions**: `first500`, `shards`, `all3352`, `asm-first500`, `asm-shards`, `asm-all3352`, `strict500` and `focused-five`.

Byte-exact archived canonical sources:
- `linux-core-all3352.yml`: `c484a871807795b15c2f43de8acdaf1071fcba57` (six full 3352 compiler + assembler jobs).
- `core-first500-regression.yml`: `02627bc5f2a3dc50dbca227cde4c8157bef3f043` (strict500 and focused-five).

Both previous owners listened on Runtime and Performance development branches with the same relevant compiler/source/test/helper path filter; only their workflow filename entries differed. The retained canonical union includes both original scopes and the historical changed-YAML path under the disabled archive.

Manual modes `compile` (unchanged default), `assemble`, `strict500`, `focused-five`, `all`. All old commit-message opt-in tags and all eight full executable job definitions are byte-identical. `all` now requests all eight independent tests, an intentional comprehensive union. One can manually select strict500 or focused-five for the old focused proof. The separate `linux-core-shards-v1.yml` crash/TU diagnostics and the cheap structural M0 remain separate.

`tools/ci/check_linux_core_3352_first500_union_v1.py` checks SHA, eight complete job bodies, trigger branches, path scope, selectors, and opt-in tags. The earlier runtime M0 separately verifies the original assembler jobs in the six-job full corpus and the original archived focused-five GNU-object steps; those checks continue on the unified owner.

M0 SUCCESS is T0 source-structure proof, not a new strict500/3352 TU/assembly certificate. The original test runners, caches, compiler identities, artifact names, aggregation verdicts and timeouts have not changed.

Expected active workflow change: Runtime 31 to 30, Performance 32 to 31, unique names 35 to 34.
