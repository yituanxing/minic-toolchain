# Retired dormant MiniAS, MiniLD and driver workflows (2026-10-08)

Source runtime branch `80f65edd53489ef860fbce20a9479a68eec9988b`; before this operation, **73 active** and **251 disabled** workflow YAMLs (including archived subdirectories). After byte-preserving archival: **65 active** and **259 disabled**. All four remote refs remain intact. The original branch-specific triggers below reference deleted `toolchain/*` owners; each run is either impossible automatically or only reachable by manually choosing old historical behavior. The check/test sources remain available.

- `minias-linux-ground-truth-inventory.yml`: blob `2e4236380c5c673ea9527e1bb542dc41c78360e5`
- `minias-linux-runtime-gcc-single-variable.yml`: blob `9e2d0a7fa5e1d8cc853c1df33c49cc4030b6a133`
- `minias-linux-sidecars.yml`: blob `fc37e277e5abd7744a511d377b494c62e6316869`
- `minias-semantic-oracle-smoke.yml`: blob `c94da101d4c0cd119bbb5cbc71fd830aed8014a6`
- `minild-busybox-static.yml`: blob `0f0848aeace781daacb731ec1ad379b28d5f5ad4`
- `minild-sqlite-static.yml`: blob `dd38308adaed581f5ea7cbd853e66d8c8e2ab5c9`
- `minild-musl-shared-rebase.yml`: blob `e500df6d4ca60ab031ac691e0a42cd062da03938`
- `minic-driver-musl-headers-v0.yml`: blob `df98fe50789839d7333fa80dddcfe0c4da0ca40e`

These tests have **distinct historical contracts**. Archiving them does not claim they are superseded or redundant in verification coverage; it deactivates abandoned trigger ownership. The modern `minias-a0-gate-v1.yml`, `minias-semantic-oracle3536.yml`, `miniar-regressions-v1.yml`, `minild-regressions-v1.yml`, `minic-rv64-focused-regressions-v1.yml`, full3352 producer and all Linux Image/QEMU certification gates remain active. Restore a dormant contract by copying its exact blob back to the active directory and deliberately updating branch/certificate provenance before using it as a current acceptance gate.

The proven 55-patch performance candidate's 3352 Linux TU compile pass is run [37764736667](https://github.com/yituanxing/minic-toolchain/actions/runs/37764736667). Valid preprocessed inputs were guaranteed by serial Kbuild materialization on all shards; both previously damaged Serio inputs passed GCC syntax checks before MiniC. Production Runtime still retains its old 39-patch profile; linked Linux Image/QEMU certification for the new compiler remains pending. The ongoing distributed Image experiment must not be equated with a candidate full55 Image certificate.
