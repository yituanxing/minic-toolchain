# Historical dead-branch Linux diagnostic retirement

This cleanup retires active GitHub Actions workflows whose automatic push scopes point only at branches that no longer exist after repository branch consolidation.

The repository currently retains `main`, `agent/linux-expanded-kbuild-v0`, `agent/ci-runtime-cleanup-v1`, and `archive/all-progress-2026-10-04`. None of the historical branch filters below is live.

## linux-asm-symbolic-pool-v0.yml

The workflow is scoped to the deleted `agent/linux-undef-pool-v0` branch and was used as a September deferred-asm symbolic failure-pool diagnostic.

Its last recorded run, `34806331293` on 2026-09-14, completed SUCCESS. That run remains the historical certification for the one-off pool.

## linux-link-correctness-v0.yml

The workflow is scoped to the deleted `agent/linux-link-correctness-v0` branch and compares selected RISC-V PI GCC/MiniC symbols and relocations.

Run `34705254969` on 2026-09-12 completed SUCCESS. The workflow is no longer part of the current runtime branch's active validation chain.

## linux-link-failure-pool-v0.yml

The workflow is scoped to deleted historical branches `agent/linux-link-correctness-v0` and `agent/linux-local-integer-isolate-v1`. It was a compiler failure-pool localization tool, not a current runtime contract.

Run `34764255231` on 2026-09-13 completed SUCCESS for the closed Linux failure pool.

## linux-filter-forensics-v0.yml

The workflow is scoped to the deleted `agent/linux-link-correctness-v0` branch and was a one-off forensic pass mapping suspicious references back to generated functions.

Run `34705254975` on 2026-09-12 completed SUCCESS. Its evidence belongs to the closed link-correctness investigation rather than the maintained runtime contract.

## linux-pi-all-v0.yml

The workflow is scoped to the deleted `agent/linux-link-correctness-v0` branch and compares GCC/MiniC undefined-symbol sets for the six historical RISC-V PI objects.

Run `34705254945` on 2026-09-12 completed SUCCESS. The exact September comparison remains recoverable from the archived YAML and Actions history.

## linux-remaining4-evidence-v0.yml

The workflow is scoped to the deleted `agent/linux-remaining4-evidence-v0` branch and existed to preserve intermediates for the then-remaining four Linux blockers.

Run `34764247976` on 2026-09-13 completed SUCCESS after the failure pool was closed. The fixed four-blocker capture is historical evidence, not a current regression gate.

## linux-undef-batch-v0.yml

The workflow is scoped to the deleted `agent/linux-undef-batch-v0` branch and was an intermediate full undefined-symbol failure-pool batch diagnostic.

It never established a successful certification run in its recorded September sequence; the last recorded run, `34837737997` on 2026-09-14, completed FAILURE. It is therefore retired as an incomplete historical diagnostic rather than represented as a passing gate.

## Retirement rule

No claim is made that one newer workflow is a byte-for-byte or invariant-for-invariant replacement for these historical experiments. Current runtime and compiler regression workflows validate the maintained toolchain paths, while these old diagnostics are preserved verbatim under `.github/workflows-disabled/` so their September experiments remain recoverable from repository history without remaining active Actions entry points.
