# Undef diagnose producer retirement

This cleanup retires the dispatch-only `linux-undef-diagnose-v0.yml` workflow after its cache/fixture consumers were retired.

## Historical role

The workflow prepared and refreshed the historical undefined-symbol diagnosis baseline under the cache key:

`linux-undef-diagnose-base-v1-6.6.143-riscv-defconfig-${{ runner.os }}`

and compiled representative objects with MiniC intermediates for the old undef/residual investigation.

Its historical consumer family included the memory/shmem/residual replay and source-context diagnostics that were later retired after the stronger 73-object real-Kbuild refresh closed the residual undefined-symbol set.

## Active-consumer audit

At cleanup/runtime HEAD `aa44010b1884d72507294765850106cab2bd7203`, every active workflow under `.github/workflows/` was scanned for the cache-key prefix `linux-undef-diagnose-base-v1`.

There were 89 active workflow YAML files at that HEAD.

Result:

- active consumers other than the producer itself: **0**
- producer self-reference: `linux-undef-diagnose-v0.yml`

The former active consumer workflows had already been retired, including:

- `linux-memory-fast-v0.yml`
- `linux-shmem-fast-v0.yml`
- `linux-shmem-fixture-export-v0.yml`
- `linux-undef-residual-replay-v0.yml`
- `linux-undef-source-context-v0.yml`

Their retirement and the later real-Kbuild closure are documented by `runtime-residual-undef-retirement.md`.

## Retirement decision

The dispatch-only producer is preserved byte-for-byte as:

`.github/workflows-disabled/linux-undef-diagnose-v0.yml`

and removed from the active Actions set.

This change does not delete any existing GitHub Actions cache, workflow run, artifact, commit, or historical consumer YAML. It only removes an orphaned manual producer entry point whose cache has no active consumer.
