# MiniPP legacy Linux focus retirement

This cleanup retires the original `minipp-linux-focus.yml` failure-pool workflow after the MiniPP focus lane was replaced by the maintained cached V1 workflow.

## Historical workflow

`.github/workflows/minipp-linux-focus.yml` was the August MiniPP convergence lane. It replayed a small Linux 6.6.143 failure pool while the preprocessor was still closing the first-500 / product-72 class of issues.

Its final revision was commit `f8cf2552a3eb7729ee8032d52ee1d3642ab7f90d`. GitHub Actions run `33321752657` on branch `toolchain/minipp-converge-500d` completed SUCCESS.

The workflow's push trigger is limited to:

`toolchain/minipp-*`

After branch consolidation the repository has only:

- `main`
- `agent/linux-expanded-kbuild-v0`
- `agent/ci-runtime-cleanup-v1`
- `archive/all-progress-2026-10-04`

So the old lane no longer has a live push branch.

## Canonical replacement

`.github/workflows/minipp-linux-focus-v1.yml` is the maintained cached focus lane. It is explicitly wired to both active development branches, freezes the Linux/GCC reference corpus, verifies corpus identity, and replays the focused MiniPP pool from that certified corpus.

Its latest consolidation verification at commit `7d5e6d42ed2fc455010f80ae12b24927e7ffe661` was GitHub Actions run `37330573070`, which completed SUCCESS on `agent/ci-runtime-cleanup-v1`.

The V1 lane is therefore the canonical focused Linux replay entry point.

## Retirement decision

`.github/workflows/minipp-linux-focus.yml` is preserved byte-for-byte under `.github/workflows-disabled/minipp-linux-focus.yml` and removed from the active Actions set.

The maintained `.github/workflows/minipp-linux-focus-v1.yml` remains active.

No historical YAML, commits, workflow runs, or uploaded artifacts are deleted.
