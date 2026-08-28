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
