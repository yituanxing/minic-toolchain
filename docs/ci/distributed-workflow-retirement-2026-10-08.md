# Distributed Linux workflow retirement — 2026-10-08

## Scope / safety model

This cleanup is restricted to **four workflow definitions**, and does not
modify source/compiler/runtime/linker/test implementation, old job evidence,
Git history, cache identities, the four remote branches, the existing
fast graph preflight, or any runtime isolation/certificate workflow.
Each YAML was copied **byte-for-byte** to `.github/workflows-disabled/`,
its Git blob SHA was compared to the active original, **then** the active
copy was deleted. Every retired workflow can be restored from the original
archive path or any parent commit.

### Verified workflow inventory

- Before: **68** active workflow YAML files, **260** disabled files.
- After: **64** active workflow YAML files, **264** disabled files.
  (Disabled count includes 221 direct files and 43 under two old directories.)
- The four surviving branches are `main`,
  `agent/linux-expanded-kbuild-v0`, `agent/linux-perf-boolean-domain-v1`,
  `archive/all-progress-2026-10-04`.
- All 68 formerly active workflow YAML files were read and scanned for
  literal references to the historical producer run IDs 37767282927 and
  37787452745, old receiver workflow filenames and old 120-shard names.
  **No other active workflow** relied on those historical pinned receivers
  or the old 120-shard artifacts. This does not claim cross-file references
  in arbitrary source/doc files are absent; old links remain archival.

## Retired exact sources

| Old live workflow filename | Original/archived Git blob SHA | Why safe to retire |
| --- | --- | --- |
| `linux-distributed-kbuild-objects120-v1.yml` | `9185bee34ba0f4391561f6a22e294e1a32675efd` | Six-runner 120-object transport/partial link proof had [real green run 37762351340](https://github.com/yituanxing/minic-toolchain/actions/runs/37762351340); 3352-object strict transfer/relink run 37796369453 has stronger current transfer/identity coverage; supporting 120-script and target TSV preserved |
| `linux-distributed-image-receiver-reuse-v1.yml` | `fcff21d5ee82841c4c94c72a16009e8d5b2165aa` | One-shot signedness/mtime regression diagnosis hard-pinned to old shard producer run 37767282927, and provenance plus signedness follow-ups now have receiver evidence; diagnostic source/history preserved |
| `linux-distributed-image-receiver-signedness-v2.yml` | `7de981f45db79ef58e8e45caf65b3fa4f015f0ef` | Receiver Strict Replay V4 used pinned 37787452745 4-day shard artifacts; its strict logic was migrated into the self-generating canonical distributed Image workflow; [V4 receiver run 37796369453](https://github.com/yituanxing/minic-toolchain/actions/runs/37796369453) confirms successful strict Image link but failed QEMU |
| `linux-distributed-full-image-v1.yml` | `3a91435760197edacddc277395332c9023b808da` | Manual older 39+ICE full-Image experiment. Historical [run 37764394522](https://github.com/yituanxing/minic-toolchain/actions/runs/37764394522) found Nouveau constant-p problems; the later signedness/correctness profile now owns this full graph → shards → receiver → QEMU structure; old exact workflow remains archived for historical 39+ICE comparison |

## Current canonical ownership

**Maintained full Image and QEMU owner:**
`.github/workflows/linux-distributed-full-image-signedness-v2.yml`.
Its display name is **Linux Distributed Full Image Strict Replay**.
It independently prepares the full Linux graph, produces seven true MiniC
shards, verifies all transferred objects, runs dependency warmup, restores
producer bytes, validates the one known `.delay.o.cmd` command correction,
requires **zero planned-target recompiles on the final Kbuild pass**, verifies
all reconciled 6704 records, and then requests QEMU. It does not have
`run-id:` dependencies on expiring historical producer artifacts.

**Separate inexpensive plan owner retained:**
`.github/workflows/linux-distributed-image-graph-preflight-v1.yml`.
It produces/verifies the Linux Kbuild target manifest without the long
shard compilation and is not replaced by the full Image run.

**Other intact certification owners:** Linux runtime GCC oracle, link fixture,
frozen/frontier/FDT/kallsyms/watcher/isolation gates, M0, full frozen 3352
corpus, MiniPP, MiniAS, MiniAR, MiniLD, performance checks.
We do **not** declare these redundant based on file names or skipped push runs.

## Existing proof versus remaining proof

- Exact [3352 MiniC C objects from seven shards](https://github.com/yituanxing/minic-toolchain/actions/runs/37787452745):
  complete real-Kbuild C object production.
- [Receiver strict replay V4](https://github.com/yituanxing/minic-toolchain/actions/runs/37796369453):
  restored 3352 objects; SHA checked original 6704 records; warmup
  recompiled 37; strict restoration and documented single-metadata
  normalization; final Kbuild `target_recompiles=0`, `verified_files=6704`,
  and `DIST_IMAGE_LINK=PASS` on a 215502848-byte Image. The full CI is
  **not green** because QEMU timed out after OpenSBI-only output.
- The consolidated *self-generating workflow* has passed M0 static checks
  [37801576684](https://github.com/yituanxing/minic-toolchain/actions/runs/37801576684)
  but **has not yet** independently run the seven-shard cycle on the
  consolidated YAML HEAD.
- A passing end-to-end full Image build/reuse check on current HEAD remains
  an explicit acceptance gate **before runtime issue localization**.
  Even after Image cert passes, the QEMU boot marker remains a separate
  currently failing runtime requirement.

## Follow-up discipline

Do not remove historical scripts or toolchain profiles merely because their
workflow front-ends are archived. Do not add another receiver-only workflow
for each failed runtime experiment; reuse the maintained Image producer and
the existing runtime isolation/watcher chain. Further consolidation of the
remaining 64 active workflows requires per-workflow unique contract review,
artifact/cache consumer audit and verified supersession evidence.
