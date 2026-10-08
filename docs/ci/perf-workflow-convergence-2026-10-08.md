# Performance CI convergence — 2026-10-08 / 性能 CI 收敛

Branch `agent/linux-perf-boolean-domain-v1` @ `99da409db57c1fab206e9fc70fe0918e79a6cae5` had 98 active workflow YAML files versus 87 on the runtime branch. This change archives seven experiment-only workflows *verbatim*, leaving 91 active workflow YAML files on this branch. Compiler sources, tests, and runtime certification workflows are unchanged.

## Archived exact workflows / 原样归档
- `linux-core-object-address-first500-ab-v1.yml` (blob `c061d02da1bff89331224392c4331d6c8ee39bb4`)
- `linux-core-object-address-top5-ab-v1.yml` (blob `d7c568fb1f42d8f926e833be71e973d447816fcd`)
- `linux-core-object-color-top5-ab-v1.yml` (blob `30983d4e00d0a27c6ce421a765e558d4db8c914c`)
- `linux-core-object-slot-top5-audit-v1.yml` (blob `cae3885e1320525771fe56ec37d554b93747015f`)
- `linux-current-profile-first500-perf-v1.yml` (blob `3bc1b51a07f4eb3b1a897683af866c60c033ecf5`)
- `linux-top5-phase-profile-v1.yml` (blob `35e6922f71ce6fef01e4724744c7808091f94907`)
- `linux-core-object-interval-first500-ab-v1.yml` (blob `be06990363e5c2a1b41f0d8aed6dc9857247dedc`)

These focused Top5/first500 A/B experiments and historical profile probes are not currently a unique long-term CI owner. Their original scripts, commits and CI runs remain discoverable. The separate forced-fallback assertion in `linux-core-object-interval-top5-ab-v1.yml` is retained, not retired.

## Kept and protected / 保留的门禁
- `linux-optimized-first500-verify-v1.yml`: integrated 500-TU A/B; changed from every push to source/test/CI changes only.
- `linux-parser-scope-first500-ab-v1.yml`: parser-scope 500-TU A/B + GCC/RISC-V QEMU cases; changed from every push to relevant inputs only.
- `linux-gnu-constant-p-ice-regression-v1.yml`: GNU ICE semantic/QEMU regression retained.
- All active Linux runtime, verified frozen fixtures, full-Image and QEMU certification workflows are unchanged.

## Safety / 后续
- A `3352/3352` `.i -> .s` compile success is not evidence for an assembled, linked or booted Linux Image.
- The 27 development branch tips were separately captured by archival octopus commit `458f42b98ecc707b63c12208aa6d2fcb54258be3`. This operation does **not** delete any remote branch refs.
- Before future workflow removal, prove that its unique invariant is obsolete or migrated and that cache/certificate consumers remain valid.
- The historical `docs/ci/current-workflow-map.md` reflects the runtime convergence branch; do not claim its 87-workflow inventory is current for every experiment branch.
