# MiniAS Linux input inventory retirement

This cleanup retires the historical `.github/workflows/minias-linux-input-inventory.yml` workflow after the MiniAS native Linux workload was expanded and made self-contained in the canonical A0 gate.

## Historical inventory

The workflow materializes a Linux 6.6.143 RISC-V defconfig dry-run Kbuild plan and classifies configured compile rules by source type.

Its recorded successful inventory run is GitHub Actions run `33237625114` at commit `d0601327e2a32e38cbaafb094a119166768686e7`. It completed SUCCESS and reported:

- C translation units: 3352
- preprocessed native assembly inputs: 26
- raw native assembly inputs: 0
- Rust inputs: 0
- native assembly total: 26
- assembler inputs without Rust: 3378
- configured object compile rules: 3378

That result describes the historical native26 workload discovery stage.

## Maintained native workload

The maintained `.github/workflows/minias-a0-gate-v1.yml` no longer consumes the inventory artifact. Its native35 job reconstructs the certified Linux 6.6.143 RISC-V native assembly workload directly from targeted Kbuild object commands in the same run.

Canonical full-gate run `37400148037` completed SUCCESS. Its native35 job records:

`MINIAS_NATIVE35_SUMMARY pass=35 fail=0 total=35`

The same run also passed all seven frozen C shards, C149, all3352, and the final gate3536 aggregate.

`.github/workflows/minias-semantic-oracle3536.yml` independently reconstructs the same 35-target native workload for semantic comparison and likewise does not consume the historical input-inventory artifact.

No active maintained MiniAS workflow depends on `minias-linux-assembler-input-inventory`.

## Retirement decision

`.github/workflows/minias-linux-input-inventory.yml` is preserved byte-for-byte under `.github/workflows-disabled/minias-linux-input-inventory.yml` and removed from the active Actions set.

This does not retire `minias-linux-ground-truth-inventory.yml` or `minias-linux-sidecars.yml`; those workflows still provide frozen artifacts consumed by maintained MiniAS gates and remain active.

No historical YAML, commits, workflow runs, or uploaded evidence are deleted.
