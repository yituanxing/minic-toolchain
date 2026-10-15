# IDR/XArray runtime retirement contract

This cleanup retires the historical `linux-idr-xarray-runtime-v0.yml` A/B workflow after proving that its recorded failures are tied to an obsolete runtime patch stack and that the maintained current-profile frontier path rebuilds the same owners without introducing a regression beyond the authoritative baseline.

## Historical A/B lane

The workflow compared two MiniC configurations while rebuilding only:

- `lib/idr.o`
- `lib/xarray.o`

Its final historical revision was commit `190ec23c5f5822b67d83284a4092dc474338f05c`.

GitHub Actions run `35320118231` failed in both matrix modes:

- `baseline-idr-xarray`
- `reuse-idr-xarray`

Both owner rebuilds completed. Both failures occurred only in the runtime first-fault analyzer, and both stopped at the same historical global frontier:

`fault_load:strlen+0x6`

Neither path reported an IDR/XArray-specific fault, bad-stack path, panic path, null-page fault, or IRQ-guard fault.

## Current re-run of the historical workflow

The historical workflow was re-run at current cleanup/runtime history via commit `979ae5a9fc3f03dfd441b6aab92bd2dcefdeb746`, run `37500833588`.

The result was intentionally diagnostic and again concluded FAILURE in both matrix modes, but the evidence was identical to the September result:

- both MiniC builds succeeded;
- both `lib/idr.o` / `lib/xarray.o` rebuilds succeeded;
- baseline runtime stopped at `fault_load:strlen+0x6`;
- reuse runtime stopped at `fault_load:strlen+0x6`;
- no IDR/XArray-specific new fault appeared.

This repetition established that the workflow itself still reproduces its old global frontier rather than a current IDR/XArray regression.

## Why the historical lane is obsolete

The historical workflow carries an inline patch list rather than the maintained centralized runtime profile.

Compared with `tools/ci/linux-runtime-build-minic-profile-v1.sh`, it is missing five later runtime fixes:

- `apply-riscv64-record-call-result-snapshot-reuse-v0.py`
- `apply-riscv64-core-object-slot-reuse-v0.py`
- `apply-riscv64-core-object-slot-reuse-cfg-hotfix-v0.py`
- `apply-riscv64-structured-asm-used-callee-save-v0.py`
- `apply-riscv64-pi-local-symbol-address-v0.py`

Therefore its baseline/reuse A/B no longer represents either side of the current production runtime compiler profile.

## Current-profile owner proof

The maintained `linux-runtime-frontier-fast-v5.yml` workflow explicitly replays the same two owners:

- `lib/idr.o`
- `lib/xarray.o`

through the centralized current runtime MiniC profile.

Its stale frontier-contract assertion was first corrected at commit `3280563f3075d14f439a042bf0dcc58957366a44` so it follows the authoritative `linux-runtime-frontier-v1.json` baseline instead of hard-coding the historical `fdt-path` marker.

Run `37501374567` then established:

- frozen fixture verification: PASS;
- compact fixture verification: PASS;
- authoritative contract: `FAST_V5_CONTRACT=PASS baseline=paging-init pass_after=mm_core_init`;
- centralized current MiniC runtime profile build: PASS;
- current `lib/idr.o` replay: PASS;
- current `lib/xarray.o` replay: PASS;
- `EARLY_LINK_V2=PASS`;
- QEMU watcher completed normally;
- runtime classification: `FRONTIER_VERDICT=SAME_FAULT`;
- normalized fault: `fault_load:strlen+0x6`.

The workflow run itself concludes FAILURE by design when the classifier returns rc=1 for `SAME_FAULT`: Fast V5 is a progress gate and only becomes green when the runtime advances beyond its certified baseline. The failure is therefore not an owner-build failure and not an IDR/XArray-specific regression.

For IDR/XArray retirement purposes, the relevant invariant is that rebuilding those exact owners with the current centralized profile preserves the authoritative baseline and introduces no earlier or different fault.

## Retirement decision

The historical A/B workflow is preserved byte-for-byte as:

`.github/workflows-disabled/linux-idr-xarray-runtime-v0.yml`

and removed from the active Actions set.

Current IDR/XArray coverage remains in the maintained current-profile frontier/fault-context workflows that rebuild the same two owners.

No historical YAML, commits, workflow runs, artifacts, or evidence are deleted.
