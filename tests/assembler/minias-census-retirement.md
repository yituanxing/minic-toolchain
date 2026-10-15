# MiniAS census workflow retirement

This cleanup retires the historical MiniAS assembly-census workflows after auditing their role against the maintained 3536-input MiniAS acceptance gate.

## Historical census workflows

### minias-m0-census.yml

This workflow was created on 2026-08-28 to measure the assembly surface generated from the frozen Linux corpus. It imported the historical frozen corpus export from run `33186855250`, regenerated assembly with the then-current compiler, and produced census reports for first500 and the remaining frozen shards.

Its final revision was commit `b44399bf9ac73e3f24c2edc2f2a620cbdab39e67`. The final recorded workflow run, `33188551917`, completed FAILURE.

The workflow is an inventory/measurement diagnostic, not a fail-closed MiniAS acceptance gate.

### minias-m0-census-smoke.yml

This smaller workflow profiled a 16-input sample from the frozen first500 corpus to inspect generated assembly size and line shape.

Its final revision was commit `e635f995a7da3c712bce34b0c454c0a5e112e27a`. The final recorded workflow run, `33188809428`, also completed FAILURE.

No active maintained MiniAS workflow consumes either census workflow's output artifacts.

## Current canonical acceptance evidence

`.github/workflows/minias-a0-gate-v1.yml` is the canonical fail-closed MiniAS Linux acceptance gate.

GitHub Actions run `37400148037` completed SUCCESS and independently passed:

- first500: 500 inputs
- new500: 500 inputs
- next500: 500 inputs
- next500b: 500 inputs
- next500c: 500 inputs
- next500d: 500 inputs
- final352: 352 inputs
- all3352 aggregate
- ground-truth C149
- native35
- final `gate3536`

The maintained gate therefore validates the complete certified workload at one repository HEAD rather than merely measuring the generated assembly surface.

The maintained diagnostic and semantic surfaces remain active:

- `minias-a0-window.yml`
- `minias-a0-focused-diagnostics-v1.yml`
- `minias-semantic-oracle3536.yml`
- `minias-semantic-oracle-smoke.yml`

The semantic-oracle smoke workflow is intentionally retained because it contains a dedicated comparator self-test with an intentionally mutated ELF pair, which is not equivalent to the historical census smoke.

## Retirement decision

The following workflow files are preserved byte-for-byte under `.github/workflows-disabled/` and removed from the active Actions set:

- `minias-m0-census.yml`
- `minias-m0-census-smoke.yml`

The frozen corpus export artifacts from run `33186855250` are not deleted by this change. At the time of this audit they remain available, but the retired census workflows are not part of the maintained acceptance contract.

No historical YAML, commits, workflow runs, or uploaded artifacts are deleted.
