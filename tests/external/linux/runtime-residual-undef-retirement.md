# Linux residual undef replay retirement contract

This document records why the September residual undefined-symbol replay cluster can be retired from active GitHub Actions without losing its historical evidence.

## Historical residual contract

`.github/workflows/linux-undef-residual-replay-v0.yml` froze 19 Linux 6.6.143 RISC-V preprocessed translation units and replayed them through the then-current MiniC compiler. Its purpose was to detect whether a fixed set of historical unresolved symbols still appeared in generated assembly.

The 19 object owners were:

- `crypto/skcipher.o`
- `drivers/acpi/scan.o`
- `fs/proc/task_mmu.o`
- `kernel/bpf/verifier.o`
- `kernel/dma/direct.o`
- `kernel/dma/pool.o`
- `kernel/jump_label.o`
- `kernel/ptrace.o`
- `mm/gup.o`
- `mm/mremap.o`
- `mm/page_alloc.o`
- `mm/percpu.o`
- `mm/slab_common.o`
- `mm/swap.o`
- `mm/truncate.o`
- `mm/vmstat.o`
- `net/core/bpf_sk_storage.o`
- `net/core/skbuff.o`
- `net/ipv6/icmp.o`

The final recorded residual replay on the historical diagnose branch was run `34854381962` on 2026-09-14. It completed FAILURE, so that workflow is not represented as a passing certification gate.

The companion historical workflows were:

- `linux-memory-fast-v0.yml`: focused `mm/memory.o` replay.
- `linux-shmem-fast-v0.yml`: focused `mm/shmem.o` replay.
- `linux-shmem-fixture-export-v0.yml`: artifact export for the frozen shmem fixture.
- `linux-undef-source-context-v0.yml`: source-context printer for the residual symbol list.

At the final September diagnose HEAD, the focused memory, shmem, fixture-export, and source-context workflows completed successfully while the broader 19-object residual replay still failed.

## Superseding real-Kbuild evidence

The later maintained `.github/workflows/linux-expanded-kbuild-v0.yml` contains a larger final-link failure-pool refresh. At commit `0343e4819298b51f0ee644a5d3b1550ce7a82d90`, the pool contains every one of the historical 19 residual objects, plus both `mm/memory.o` and `mm/shmem.o`, inside a 73-object real-Kbuild refresh.

GitHub Actions run `35113672803` on 2026-09-16 completed SUCCESS. Its `image-frontier` job records:

- `LINUX_UNDEF_REFRESH_TARGETS=73`
- `LINUX_UNDEF_REFRESH=PASS`
- the full Linux Image build completed with `rc=0`
- `undefined_refs=0`
- `undefined_unique=0`
- `LINUX_EXPANDED_IMAGE=PASS`

The job did not skip the failure-pool rebuild through an exact mixed-cache hit: the log records `KBUILD_CACHE_HIT=false`, then performs the 73-object refresh before the successful Image link.

This is a stronger closure than the old preprocessed replay because the historical residual owners are rebuilt through the real Kbuild wrapper and then participate in the final linked Linux Image with zero remaining undefined references.

## Retirement decision

The following historical workflows are therefore retired from the active Actions set and preserved byte-for-byte under `.github/workflows-disabled/`:

- `linux-undef-residual-replay-v0.yml`
- `linux-memory-fast-v0.yml`
- `linux-shmem-fast-v0.yml`
- `linux-shmem-fixture-export-v0.yml`
- `linux-undef-source-context-v0.yml`

The manually dispatched `linux-undef-diagnose-v0.yml` remains active for now. It is a separate representative diagnostic entry point and is not required for this retirement.

No historical YAML or Actions evidence is deleted by this change.
