# Historical crc32 runtime probe retirement

This cleanup retires two September runtime diagnostics that were built specifically around the former `crc32_body` first fault.

## Historical probes

### linux-runtime-crc32-differential-v0.yml

The workflow was introduced to diagnose the post-kallsyms crc32 regression by comparing the current `lib/crc32.o` against a baseline object and capturing runtime state around `crc32_be` / `crc32_body`.

Its last workflow revision was commit `b5fe5d658a78861ac05e312ae43cba63c0dae63d`. GitHub Actions run `36661192368` completed SUCCESS and recorded the then-current first fault as:

`FRONTIER_FAULT=load_page_fault:crc32_body+0xdd4`

The baseline swap in the same diagnostic still faulted in `crc32_body`, at `+0x191a`. The workflow therefore served as a historical A/B localization experiment rather than a general current-frontier regression contract.

### linux-runtime-dtb-pointer-probe-v0.yml

This workflow reproduced the same full-owner image and captured the exact crc32 fault frame, DTB-related symbols, and stack/register state around the faulting instruction.

Its last workflow revision was commit `75d0243a3c227a58c8a928a4136e95a2ba88cfa2`. GitHub Actions run `36752499856` completed SUCCESS and again classified the first fault as:

`FRONTIER_FAULT=load_page_fault:crc32_body+0xdd4`

It additionally captured the exact GDB fault site at `crc32_body+3540`.

## Current frontier evidence

The synchronized cleanup/runtime HEAD `86da8ee4bc029431939ca0e6206126dc3ac15840` has a current generated-kallsyms certification run, `37483502587`, which completed SUCCESS.

That run rebuilt the current runtime owners, generated two consistent kallsyms passes, and reached first die successfully. Its current first-die result is:

- `FIRST_DIE_HIT=YES`
- `CAUSE_NAME=load-page-fault`
- `EPC_NEAREST=fdt32_ld+0x94`
- `GET_SYMBOL_POS_FAULT=NO`
- `GENERATED_KALLSYMS_FIRST_DIE=PASS`

The current runtime frontier therefore no longer reproduces the historical `crc32_body+0xdd4` fault that these two probes were designed to investigate.

General current fault localization remains available through maintained workflows such as `linux-runtime-fault-context-v1.yml`, `linux-runtime-generated-kallsyms-first-die-v0.yml`, and the current QEMU watcher contracts.

## Retirement decision

The following workflow definitions are preserved byte-for-byte under `.github/workflows-disabled/` and removed from the active Actions set:

- `linux-runtime-crc32-differential-v0.yml`
- `linux-runtime-dtb-pointer-probe-v0.yml`

No historical YAML, commits, Actions runs, or uploaded evidence are deleted by this change. `linux-expanded-entry-trace-v0.yml` and `linux-runtime-riscv-init-codegen-v0.yml` remain active because they provide generic tracing and independent code-generation evidence rather than targeting only the retired crc32 fault.
