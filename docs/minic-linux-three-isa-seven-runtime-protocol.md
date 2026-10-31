# MiniC Linux 6.6.143: immutable GNU → 3 × 7 → GNU LD/QEMU protocol

**Operational contract, 2026-10-31.** This records the *existing* historical
RV/MiniAS method and current MiniC implementation; it does not certify a new
run. Authoritative entrypoint: `.github/workflows/minic-linux-three-isa-seven-runtime-v1.yml`.
Receiver: `tools/ci/linux-three-isa-seven-runtime-v1.py`; strict MiniC C
candidate: `tools/ci/linux-three-isa-minic-candidate-v1.py`.

## Why the gates exist (never conflate their verdicts)

| Gate | Operation / original evidence | Why it exists / what it **does not** prove |
| --- | --- | --- |
| 0. Exact HEAD | One-shot trigger marker or dispatch; pin commit; build **one** `minic` + sibling `minic-cc` pair | Prevent duplicate full campaigns and mixed compiler revisions. `gate=success` is authorization only. |
| 1. Real GNU golden | Linux 6.6.143 SHA-pinned archive, real target GNU Kbuild/broad `.config`, GNU `.o.cmd` C-owner census, original GNU Image, independent GNU QEMU **P0 and P1** | Establishes a *working same-target reference* before replacing C code. GNU-only PASS is not MiniC PASS. |
| 2. Freeze original C inputs | Per-owner original GNU `-E` `.i`, `.cmd`, original GNU `.o`, owner order/index/shard, command/flag SHA, ABI, optional `.incbin` sidecars | No source selection guesses; original Kbuild options, true C ownership and generated dependencies remain fixed. No second GNU preprocessing on candidate cache replay. |
| 3. Three by seven | For **each** x86-64, AArch64, LoongArch64, partition `rows[shard::7]`; MiniC compiles every original frozen C input to `.s`, original target GNU AS produces `.o` | Seven *non-overlapping complete* target C partitions, 21 independent workers; MiniAS is a method reference, **not** the assembler under test. 21/21 only proves MiniC C → GNU AS. |
| 4. SHA-verified transport | Before expensive receiver, verify all seven identities, GNU source epoch/`.i`/`.cmd`/object SHA, per-object `asm_sha256`/`obj_sha256`, target ELF e_machine/ABI, sidecars, no missing/duplicate owners | `preflight` rejects partial/foreign artifacts; `receive/collect` rechecks exact ordered frozen owner membership against restored GNU. Never infer success from an artifact filename/cache hit. |
| 5. Original GNU LD | Replace **all and only** genuine GNU C owners with seven-shard MiniC `.o` in original Kbuild O=; retain GNU source/headers/AS/LD; run genuine Kbuild final Image link | A linked ELF/Image is not a boot. Any GCC-compiled candidate C owner is forbidden. GNU non-C objects are expected. |
| 6. Dynamic C epochs | GNU Kbuild may regenerate derived C (e.g. x86 vDSO → compressed `misc.o`/`kaslr.o`); repeat actual GNU `-E` → same MiniC → GNU AS, SHA audit every changed live C owner and relink | A quiet GNU C recompilation would otherwise produce a false all-MiniC image. Require full *postlink* C census, no unexplained owner, no GCC C fallback. |
| 7. Real runtime + parity | Same-target actual QEMU candidate P0 **and** P1, compare semantic markers to fresh GNU golden; require C-option qualification | No timed-out/partial/skipped QEMU, copied log, changed source/Kconfig/firmware or ABI may count. Only all three receivers passing produces strict Linux PASS. |

Linux tar SHA-256:
`dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932`.
The broad Kconfig is **per-target resolved**; different counts are legitimate,
not permission to omit owners. Historical common/broad runs are different workloads.

## What the old RV and MiniAS experience actually established

- RV64: Stage4 `2064/2064` C owners; GNU AS/LD, real QEMU P1 `12/12`.
  Its fast iterations froze Linux inputs once and used seven parallel workers,
  then SHA-reused successful objects. RV32/ILP32 is a **different** incomplete
  kernel-runtime target, not covered by that result.
- MiniAS: frozen GNU assembly reference, ABI/flags differential, exact SHA
  artifacts and independent GNU link/runtime oracles; historical `3536/3536`
  assembly coverage must **not** be confused with MiniC's Linux C-owner count.
  Example: a MiniAS differential was initially invalid because its probe
  omitted original `-march`/`-mabi`; compare exact source command before
  blaming a backend (`docs/ci/minias-linux-earlycon-differential-2026-10-14.md`).
- RV fault isolation: start with the **full_all** MiniC image, restore a
  pristine independent GNU Kbuild snapshot before each *diagnostic* experiment,
  swap already SHA-pinned `.o` by `prefix`, `single`, `all_except`, and test
  genuine GNU link/QEMU without recompiling all C. Compare a `single` failure
  with inverse `all_except` PASS before claiming a causal owner. Mixed-object
  boots **never qualify** as full MiniC Linux runtime acceptance.

## Present pinned transport and cache boundary

| Pinned input | Workflow env / evidence | Expiration and use |
| --- | --- | --- |
| Original GNU 21 frozen input shards | `MINIC_LINUX_PINNED_INPUT_RUN=41108659343` | Source/config/`.i`/`.cmd` artifacts, **7-day** retention. GitHub reported `expired=false` on 2026-10-31; expiry **2026-11-06 18:42–18:46 UTC** (**11-07 02:42–02:46 Beijing**). Verify again before reuse. |
| Already produced MiniC 21 C-object shards | `MINIC_LINUX_PINNED_OBJECT_RUN=41133698941` | Receiver-only reuse is **opt-in**, requires current `minic` **and** `minic-cc` byte-identical (`cmp`) to producer's pinned binaries. Also 7-day retention; verify live artifacts, do not assume indefinite reuse. |

Both IDs are source *run IDs*, **not** new strict PASS claims. GNU cache keys
include target, fixed source/config profile, original Kbuild/freezer scripts,
GNU tool executable hashes and canonical work path. Restored **exact or prefix**
key bytes must pass the original SHA/ELF/manifest verification; otherwise
move partial work aside, rebuild real GNU Kbuild. Cache **pristine** GNU
before candidate overlays any `.o`; never save post-MiniC GNU build as golden.
Every restored GNU Image still needs **fresh P0/P1** with current initramfs.
When a receiver fails, preserve the **exact** `runtime-initramfs.cpio.gz`
alongside the GNU P0/P1 receipt and its checked SHA. A newly packaged initramfs
is not interchangeable, since the original cpio archive is not reproducible
byte-for-byte merely by rebuilding its programs.

The candidate shard cache is independent: only exact full owner+command+ABI+
GNU-AS+compiler identity qualifies `cache-load`, then original seven-artifact
receipts are still emitted. If the binary pair is *not* byte-identical, do
not reuse old candidate `.o`; compile against frozen inputs again. To renew
expired original inputs, explicitly trigger `[minic-regen-gnu]` and promote
the new pinned ID **only after** the matching GNU and complete runtime proofs.
For an isolated parser/receiver repair without any changed MiniC machine code,
the HEAD subject may additionally use `[minic-only-aarch64]` together with
`[minic-reuse-objects]`. This runs only the AArch64 receiver using the
byte-verified 21-shard production batch; the global strict result deliberately
reports **PARTIAL_RECEIVER_REPLAY**, never a three-ISA PASS. Resume all three
receivers when a genuine changed target object or common backend fix needs it.

## Strict acceptance versus bounded failure-only diagnosis

- **Official:** all original GNU target C owners replaced by current MiniC;
  exactly seven authenticated shards per ISA; GNU AS/Kbuild/LD; regenerated
  C through MiniC; postlink zero GNU C fallback; original GNU and candidate
  QEMU P0/P1 semantic differential; zero unqualified C-option gaps.
- **x86 object cohorts:** `tools/ci/linux-three-isa-boot-cohort-diagnostic-v1.py`
  `prepare` snapshots *pristine* GNU O= with independent reflinks before
  overlay; on an official P0 failure, `probe` reuses staged SHA-verified `.o`
  for `gnu-plus-compressed` (12 compressed C owners) and
  `minic-except-compressed` (their complement). Use original GNU Kbuild link
  and QEMU P0. Record **actual postlink C-owner SHA/origin**, link CC activity,
  overwritten selections, effective cohort, semantic observation, and
  recover original all-MiniC failed build. A regenerated owner may change
  the actual cohort: mark `DIAGNOSTIC_INCONCLUSIVE_OWNER_DRIFT`, *still run P0*,
  never label the intended cohort a proven cause or strict acceptance.
- **Exception/PC/QMP evidence:** x86 BIOS reset/PC, AArch64 EL1 exception,
  Loong EFI→Linux PC capture are **failure-only** diagnostics using original
  failed Image/firmware/receipts. A QMP/debug trace is not Linux P0/P1 success.

## Batch iteration and evidence checklist

1. Gather **all** exposed blockers in one run: each of 21 C workers, each
   GNU final linker, each fresh Image/QEMU. Group by root cause/target; use
   parallel agents for independent fixes. Do one meaningful repair batch,
   replay only affected frozen owners, then one explicit full campaign.
2. Preserve successes by immutable input SHA/cache. Do not rerun unmodified
   expensive GNU baseline or all 21 shards merely to see unchanged errors.
   Do not reduce scope to curated C files, disable strict parity or silently
   replace a failed MiniC C owner with GCC. Review changed-line totals per batch.
3. Record commit SHA/trigger/run URL; original source and Kconfig SHA, frozen
   C-owner count/order and `.i`/`.cmd`/ABI, GNU tools and both MiniC binary SHAs;
   seven result/ELF hashes per ISA; original GNU QEMU P0/P1 receipt; original
   GNU LD log and final Image SHA; dynamic-owner epochs/postlink no-fallback
   census; candidate P0/P1 logs, semantic differential and full outcome.

**Status vocabulary:** `gate=success` ≠ `21/21` ≠ `GNU_LD_PASS` ≠
`QEMU_P0_PASS` ≠ `GNU_QEMU_P0_P1_PASS_NOT_MINIC` ≠
`MINIC_QEMU_P0_P1_PASS_NO_GCC_C_FALLBACK`.
No timeouts, skipped receivers, cached old boot logs, mixed GNU C cohorts or
partial object sets can promote the last status. Historical runs are historical
evidence; do not infer the current HEAD's acceptance from them.
