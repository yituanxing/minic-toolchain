# Frozen Linux assembly-input corpus provenance

Purpose: immutable/reproducible input identity for MiniAS M0 census and later
differential assembler validation.

- Linux: 6.6.143 RISC-V defconfig corpus
- total translation units: 3352
- frozen preprocessed-input origin: historical Linux corpus caches
- export workflow run: `33186855250`
- export carrier branch: `refactor/declaration-sema-v1`
- export carrier commit: `d67be9dacbbf31ad67554dbad711d097d25b3cf7`
- MiniAS compiler base: `30e884ac9a34c4c9bdfef8f23e8121b1ff34a00a`
- canonical compiler executable after M0: `minic-cc`

The export run restored the already-frozen caches in their original branch
scope, validated each `selected-tus.txt`, generated a per-file SHA256
manifest, then packaged each corpus into a tar archive. MiniAS workflows verify
both the archive SHA256 and every extracted file against those manifests before
regenerating assembly.

| Shard | TUs | Historical cache identity | Export archive SHA256 |
| --- | ---: | --- | --- |
| first500 | 500 | `linux-focus-corpus-v1-6.6.143-rv64-defconfig-gcc13.3.0-<replay-indices-hash>` | `49cf8f48faf2696a1892867ced2600a0e592a1978fc3c41d86bc7c251e03ef3f` |
| new500 | 500 | `linux-new500-corpus-v1-6.6.143-rv64-defconfig-gcc13.3.0-indices-500-999` | `6b4b161b96c9f2cf224152ff4b5ada831fb66c9567b5f6abb4b27329eb3ea52a` |
| next500 | 500 | `linux-next500-corpus-v1-6.6.143-rv64-defconfig-gcc13.3.0-indices-1000-1499` | `dd7c93b8d9bdadc09b44cff401c1f00b7f0894a43d51b5a484d8869b6398140d` |
| next500b | 500 | `linux-next500b-corpus-v1-6.6.143-rv64-defconfig-gcc13.3.0-indices-1500-1999` | `0b2b769c3427ee42f7c133994c8578057a31b45af811993fe1e16e0407459222` |
| next500c | 500 | `linux-next500c-corpus-v1-6.6.143-rv64-defconfig-gcc13.3.0-indices-2000-2499` | `85f4a732599ae5576703a535baffe3cb716e72196bdaf7b3c960225e3952b0f1` |
| next500d | 500 | `linux-next500d-corpus-v4-6.6.143-rv64-defconfig-gcc13.3.0-indices-2500-2999` | `7d121d1268b32209d0b8fb17b0ac2dfe7780ecad3498be06486b0fe3d45be81a` |
| final352 | 352 | `linux-final352-corpus-v2-6.6.143-rv64-defconfig-gcc13.3.0-indices-3000-3351` | `31f4e305788ef63636ba520ee2b96eab46b17b2fd53b41ae94b434b78b78078a` |

The tar archives are transport objects, not source-of-truth semantics. If they
expire, the same historical caches may be re-exported only if their per-file
SHA256 manifests reproduce exactly. A changed hash set defines a new corpus
version rather than silently updating this one.

The generated `.s` files are downstream products of this frozen `.i` corpus
and the selected frozen compiler identity. They may be regenerated; they must
not be hand-edited.

## Pre-expiry original-artifact rescue

The seven inner tar SHA256 values above and the independently pinned original
Actions **outer ZIP** SHA256 values are different objects. The nine current
original artifact IDs, their ZIP digests, expiration times, and an offline
verifier/explicit downloader are recorded in
`tools/ci/minias-historical-artifacts-v1.json` and
`tools/ci/minias_artifact_rescue_v1.py`. See
`docs/ci/minias-artifact-rescue-2026-10-09.md` for the fail-closed
migration contract. M0 validates the rescue verifier, but it does not
mirror these expiring bytes or certify a heavy Linux/MiniAS run.
 
### Observed current-branch first500 restoration (2026-10-09)

The Runtime commit `544061437dad41010fe8b099fab16c6b38e3a680`
selected `[minias-real16]` through the reusable MiniAS router.
[Actions run 37922075990](https://github.com/yituanxing/minic-toolchain/actions/runs/37922075990)
completed successfully: original first500 tar SHA256
`49cf8f48faf2696a1892867ced2600a0e592a1978fc3c41d86bc7c251e03ef3f`,
16 of 16 current MiniC assembly inputs generated successfully,
and **16 of 16 assembled by MiniAS without a blocker**. This proves
original first500 artifact access and the focused 16-case owner *at
this commit*, not all 3536 semantic cases, the remaining six shards,
or durable retention after artifact expiry.
