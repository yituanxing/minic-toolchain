# Remaining43 fast-loop retirement

This cleanup retires the historical `linux-remaining43-fast-v0.yml` focused Linux tail replay.

## Historical purpose

The workflow was created during the September undefined-symbol reduction phase. By its final useful runs it no longer represented 43 unresolved objects literally; it had been narrowed to a small tail replay that rebuilt selected objects and printed their remaining undefined references.

A representative successful run is GitHub Actions run `34929457325` on 2026-09-15. It rebuilt and inspected targets including:

- `kernel/dma/direct.o`
- `mm/truncate.o`
- `fs/proc/task_mmu.o`
- `net/core/skbuff.o`
- `net/core/filter.o`

The run completed SUCCESS while reporting each target's ordinary undefined-symbol count and preserving evidence for the then-interesting residual symbols.

Commit `fb39619dbfaebc3dba7972e5fd2b439f8ec20a32` later gated the workflow behind the explicit `[linux-remaining43-fast-v0]` marker, documenting its legacy status.

## Superseding closure

The maintained `linux-expanded-kbuild-v0.yml` later moved the project from small pre-link tail replay to a larger real-Kbuild failure-pool refresh and final Image link.

At commit `0343e4819298b51f0ee644a5d3b1550ce7a82d90`, GitHub Actions run `35113672803` completed SUCCESS with:

- `KBUILD_CACHE_HIT=false`
- `LINUX_UNDEF_REFRESH_TARGETS=73`
- `LINUX_UNDEF_REFRESH=PASS`
- final Linux Image build `rc=0`
- `undefined_refs=0`
- `undefined_unique=0`
- `LINUX_EXPANDED_IMAGE=PASS`

That 73-object real-Kbuild refresh includes the historical residual owners represented by the old focused tail work. It is a stronger closure because those objects are rebuilt in their real Kbuild context and participate in a successful final Linux Image link with zero unresolved references.

## Retirement decision

`.github/workflows/linux-remaining43-fast-v0.yml` is preserved byte-for-byte under `.github/workflows-disabled/linux-remaining43-fast-v0.yml` and removed from the active Actions set.

No historical YAML, commits, workflow runs, or uploaded artifacts are deleted.
