# Historical get_current owner probe retirement

This cleanup retires `.github/workflows/linux-runtime-get-current-owner-v0.yml`, a one-off owner-localization workflow created for an older runtime frontier.

## Historical diagnostic

The workflow was introduced by commit `2b73b18b5cdb3437898f66ea107c4f76219f9032` with the explicit purpose of identifying the linked owner around `get_current`.

Its diagnostic is tied to the historical target:

- symbol: `get_current`
- hard-coded address: `0xffffffff808b6fa6`
- hard-coded disassembly window around `0xffffffff808b6f40..0xffffffff808b7040`

It scans the early linked image and selected archives for `get_current`, emits a linker map, and then intended to run the old init-IRQ frontier classifier.

The only recorded workflow run, `37036528726`, completed FAILURE. The owner-refresh and early-link phases succeeded, but the owner-identification step terminated with:

`FileNotFoundError: ... build/linux-runtime-get-current-owner-v0/early/nm.txt`

Therefore the workflow never reached its declared `GET_CURRENT_OWNER_DIAG=PASS` checkpoint and is not represented as a passing certification gate.

## Current runtime diagnostics

The maintained runtime diagnostics no longer target this historical `get_current` address.

Current post-consolidation init-IRQ bridge run `37492352866` completed SUCCESS and reports:

- `FRONTIER_VERDICT=MOVED_LATER`
- `FRONTIER_FAULT=store_page_fault:calc_global_load+0x2a`
- `FRONTIER_HIGHEST_PROGRESS=init-irq`

The maintained generated-kallsyms first-die chain has independently progressed to a first-die context in `fdt32_ld`.

General fault and owner evidence remains available through maintained workflows including:

- `linux-runtime-fault-context-v1.yml`
- `linux-runtime-first-die-context-v0.yml`
- `linux-runtime-generated-kallsyms-first-die-v0.yml`
- `linux-runtime-owner-focused-v1.yml`
- `linux-runtime-spinlock-first-context-v0.yml`

These workflows also refresh the current runtime owner set rather than depending on the one historical `get_current` address.

## Retirement decision

`.github/workflows/linux-runtime-get-current-owner-v0.yml` is preserved byte-for-byte under `.github/workflows-disabled/linux-runtime-get-current-owner-v0.yml` and removed from the active Actions set.

This retirement does not claim that the historical workflow ever passed. It preserves an incomplete diagnostic for reference while removing it from the maintained active workflow surface.

No compiler/runtime implementation, historical YAML, commits, workflow runs, or uploaded evidence are deleted.
