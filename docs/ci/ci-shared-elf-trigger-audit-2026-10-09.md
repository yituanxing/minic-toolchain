# Shared ELF change-detection gap in CI — 2026-10-09

## Failure mode found

A second audit of the five automatic toolchain regressions exposed a **false negative**, not just over-triggering. Their jobs all invoke `make ... all`, and the root Makefile compiles shared ELF objects from `elf/src/reader.c`, `elf/src/relocatable_writer.c` and `elf/src/rewrite.c` for MiniAS, MiniAR, MiniLD, MiniNM, MiniObjcopy and MiniStrip.

Before this correction, five `on.push.paths` filters omitted **`elf/**`**. A push changing only the shared reader/writer/rewrite implementation could thus modify the toolchain but skip CI that exercises it.

Corrected explicit positive trigger:

`'elf/**'`

New coverage for both development branch YAML copies:

- `minic-rv64-focused-regressions-v1.yml`
- `miniar-regressions-v1.yml`
- `minild-regressions-v1.yml`
- `minic-driver-v0.yml`
- `minipp-a0.yml` (its workflow builds the whole toolchain via `make all`, even though the primary oracle is preprocessor A0)

The existing narrow negative filters for **Runtime-only sentinel changes** remain unchanged on the three expensive suites. The new positive `elf/**` trigger is added before any negative patterns, so changes to ELF source correctly re-enable regression coverage; a commit changing only `tools/ci/runtime-timekeeping-trigger.txt` is still excluded from those suites.

## Source equivalence and safety

The earlier `tools/ci/check_ci_regression_path_isolation_v1.py` now removes exactly the new ELF positive pattern when reconstructing its six historical Git blobs, preserving its original exclusions, manual eligibility, positive inputs and **complete executable test bodies**. A new `tools/ci/check_ci_shared_elf_coverage_v1.py` verifies the two other workflows by reconstructing their exact original Git blob SHA. Both gates run in Runtime and Performance M0, alongside YAML parse checks.

There is **no change to actual compiler, ELF code or test commands** and no change to total active workflow count (30 Runtime, 31 Performance). With an ELF-only source push, the corresponding regression workflows will now be eligible and may use additional runner time: this is intended correctness coverage, unlike unrelated Runtime trigger pushes.

M0 validates the source/push contract, not a full Linux Image build or QEMU boot. A real job that ends `skipped`, `cancelled`, `INCONCLUSIVE` or only a watcher `QEMU_RC=0` is never a certified kernel boot.
