# RFC: immutable GNU Linux golden reuse without weakening MiniC runtime evidence

Date: 2026-10-30. **Status: proposal only; no workflow or acceptance change.**

This RFC concerns the original Linux 6.6.143 GNU preflight in
`.github/workflows/minic-linux-three-isa-gnu-qemu-v1.yml`. It does not enable
a cache, change a Linux Kconfig, skip a C owner, or promote GNU-only QEMU to
MiniC runtime success. An accepted candidate still requires all original C
owners to be compiled by the exact-head MiniC, assembled and linked with GNU,
and compared with real original GNU P0/P1 boots. There is **no GCC-C fallback**.

## Measured cost and compression evidence

The exact-head three-target campaign
[`41045404678`](https://github.com/yituanxing/minic-toolchain/actions/runs/41045404678)
at `0024b6d8599a4ec0c59dfabeca0043fd23996b62` launched three GNU jobs
concurrently at approximately 11:04 UTC. The original GNU Kbuild, original
`.o.cmd` owner discovery, seven-worker `GNU -E -P` freeze and manifest
verification finished at 11:18 (x86), 11:20 (LoongArch) and 11:21 (AArch64).
All three original GNU P0/P1 oracles succeeded; the subsequent MiniC candidate
GNU relink failed at approximately 11:29/11:28/11:31 respectively. Thus the
observed **wall time was about 24–27 minutes**, while summed runner occupation
was approximately 75–80 minutes. A `GNU_FROZEN_I_CACHE=PASS` marker in these
jobs means *the just-created frozen inputs passed identity and SHA checks*;
`GCC_reruns=0` means no GCC preprocessing during candidate replay. Neither
marker proves cross-run cache reuse: the full GNU QEMU workflow currently has
no restore/save cache steps.

| Target | Full pristine GNU work tree | Frozen `.i` | Original `build/` |
| --- | ---: | ---: | ---: |
| x86-64 | 9,511,341,245 B | 7,141,589,888 B | 871,994,536 B |
| AArch64 | 13,361,059,650 B | 10,468,261,315 B | 1,394,798,858 B |
| LoongArch64 | 8,753,964,141 B | 6,487,633,806 B | 768,607,947 B |

The existing **21-shard discovery** workflow uploads the same broad-profile
frozen GNU corpus plus small evidence files as ZIP artifacts with
`compression-level: 3`. The artifact sizes from
[`40986864965`](https://github.com/yituanxing/minic-toolchain/actions/runs/40986864965)
are genuine compression measurements, not an assumed gzip/zstd result:

| Target | ZIP artifact (frozen `.i` + evidence) | Ratio to frozen `.i` |
| --- | ---: | ---: |
| x86-64 | 1,036,455,801 B | 14.51% |
| AArch64 | 1,420,660,023 B | 13.57% |
| LoongArch64 | 955,447,303 B | 14.73% |
| **Total** | **3,412,563,127 B** | **14.16%** of 24,097,485,009 B |

These ratios strongly support compression of frozen `.i`, but **do not measure
`actions/cache` zstd, its upload/restore time or the compressed `build/`**.
The original GNU `build/` totals 3,035,401,341 B before compression; the
source tarball is 140,654,444 B per target and the extracted source is
1,356,608,224 B per target. A rough *non-guaranteed* three-target storage
budget, assuming frozen data compresses like the observed ZIP, each `build/`
does not compress at all, and each source tree compresses to roughly its
original tarball size, is about **7.3 GB** before metadata. It may differ
materially under zstd and may crowd out other repository caches: GitHub's
default Actions cache budget is **10 GB per repository**, not per workflow.
Do not enable all three full snapshots by default until cache occupancy and
real zstd size/restore cost have been measured.

## Cache boundary: immutable GNU producer, mutable candidate consumer

The safe boundary is after the original GNU Image and genuine P0/P1 succeeded,
but **before** `linux-three-isa-minic-candidate-v1.py` replaces any GNU C
object. The candidate intentionally mutates Kbuild `*.o` and links, so a
normal end-of-job cache save can poison the golden cache with MiniC output.

1. After installing the pinned toolchain, compute a versioned input identity:
   target/ABI and broad profile, the pinned kernel archive SHA-256, the
   original golden/freezer/owner-census script blob hashes, effective Kconfig
   policy, the resolved GNU compiler/assembler/linker and host build-tool
   executable versions or SHA hashes, reproducible KBUILD environment, host
   runner image identity, and the canonical absolute source/output work paths.
   Do **not** include MiniC HEAD in this GNU-only identity. When any required
   tool cannot be fingerprinted, fail closed to a fresh GNU build.
2. On a hit, restore the **pristine** original source tree, `build/`,
   `evidence/`, source tarball and `frozen/` under the same original work
   root. Do not rely solely on a cache key or cache action's `cache-hit` value.
3. Verify the pinned source tar SHA, effective `.config` SHA, original linked
   Image SHA and original Kbuild C-owner manifest. Verify the complete
   `gnu-target-objects.sha256` inventory against actual restored objects,
   each original `.o.cmd` provenance/flag digest, and every frozen `.i`
   hash with the existing `linux-three-isa-freeze-i-v1.py verify` contract.
   Re-enumerate the live target ELF C-owner universe; any mismatch invalidates
   the restored snapshot and requires a clean GNU rebuild. Never silently
   regenerate just a mismatched C object and call the cache exact.
4. **Run fresh original GNU P0 and P1 QEMU** on the restored Image and freshly
   generated test initramfs, using the exact target firmware/ABI. The cached
   Image is not a cached QEMU acceptance result. Record fresh source, config,
   owner, image, initramfs and firmware SHA receipts.
5. If no trusted cache exists, perform the present GNU producer unchanged,
   verify it, run original QEMU P0/P1, then use explicit
   `actions/cache/save@v4` **right there**, before candidate overlay. Do not
   rely on an end-of-job post-action save. A failed baseline must not be saved
   as accepted golden.
6. Run the **unmodified acceptance**: exact-head MiniC driver and sibling
   `minic-cc` SHA lock, all original GNU `.i` C owners through MiniC and GNU
   AS, original GNU relink, postlink live C-owner re-enumeration, hash match,
   zero GCC C fallback, and real candidate QEMU P0/P1 differential. Cache
   provenance must be recorded separately from acceptance status.

### Build-only cache as a smaller pilot

A narrower `build/`-only cache is possible (0.77–1.40 GB raw per target), but
is **not independently sufficient**: original `.o.cmd` commands and generated
headers refer to the pinned source, resolved `.config`, host build tools and
canonical working paths. Restore the matching source and original evidence,
verify every original object/manifest, and independently freeze or authenticate
the entire `.i` corpus. A restored `build/` does not by itself certify the
golden Image. This pilot may be cheaper to store, but it will not eliminate
the multi-gigabyte `GNU -E -P` freeze unless frozen sources are also reused.

## A/B rollout and stop conditions

1. First profile representative `.i` using **both** `gzip` and `zstd` at
   explicit levels, plus a complete zstd snapshot for one target. Log raw
   byte count, compressed byte count, peak disk/RSS, time to save and restore,
   without altering any candidate input. The existing ZIP3 numbers above are
   the factual baseline, not substitute zstd measurements.
2. Gate an experimental cache behind an explicit opt-in on a single target.
   Run one cold producer and one warm producer with identical pinned inputs.
   Require byte-identical config, owner lists, original object SHA inventory,
   frozen `.i` SHA inventory and original GNU Image, then fresh P0/P1 and
   exactly the same post-overlay MiniC acceptance procedure. A cache hit
   without all these checks is a failed pilot.
3. Change one fingerprinted compiler or host-tool input, Kconfig policy, GNU
   producer script, or original source digest and assert that the old cache
   cannot qualify. Negative tests must also reject a modified `.i`, `.o`,
   `.o.cmd` and GNU Image on restore. Test cache miss/rebuild after corruption.
4. Expand to three targets only if compressed footprint plus existing caches
   is safely within the configured repository cache budget **and** measured
   warm wall time beats fresh GNU Kbuild/freeze. Avoid cache thrashing and
   keep each cache key immutable. Distinguish `GNU_GOLDEN_RESTORED=PASS` from
   `GNU_QEMU_P0_P1_PASS_NOT_MINIC` and `THREE_ISA_MINIC_FULL_RUNTIME=PASS`.

The current matrix already runs its three target jobs concurrently with
`fail-fast: false`. Increasing `--workers 7` for the frozen GNU preprocessor
or MiniC candidate on the same runner is not justified without CPU/memory
timing evidence. Continue opt-in exact-HEAD campaigns rather than enabling a
full GNU rebuild on every compiler-only commit.

## Decision

**Do not change the production GNU oracle yet.** The freeze output is highly
compressible and authenticated reuse is technically feasible, but actual
zstd storage/restore cost, cache quota occupancy and warm-run evidence are
missing. Preserve the current all-owner Kbuild/GNU AS/LD/QEMU semantics and
pilot the bounded cache with the validation contract above.
