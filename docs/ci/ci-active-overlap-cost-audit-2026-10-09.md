> **Subsequent 2026-10-09 Performance consolidation:** One `linux-performance-experiments-v1.yml` replaces four temporary/experimental entries; Runtime remains **28**; Performance **26**; unique YAMLs **29**; branch-path copies **54**. **173 declared jobs** (Runtime 88, Performance 85; extra Performance route job). See [exact archived job and routing proof](ci-performance-experiment-consolidation-2026-10-09.md). The table immediately below documents the *preceding* 57-YAML snapshot.

# Active CI cost, overlap and temporary workflow review — 2026-10-09

## Baseline and measured change

| Measure | Before | After | Interpretation |
| --- | ---: | ---: | --- |
| Active Runtime YAMLs | 30 | **28** | 3 focused regressions → 1 owner |
| Active Performance YAMLs | 31 | **29** | Same consolidation |
| Distinct active filenames | 34 | **32** | Count reflects logical entrypoints |
| Both-branch YAML copies | 61 | **57** | Scope: two branch trees |
| Runtime job declarations | 88 | **88** | No regression oracle removed |
| Performance job declarations | 84 | **84** | No regression oracle removed |
| Both-branch job declarations | 172 | **172** | These are declarations, not 172 simultaneous runner jobs |

The three canonical T1 jobs run concurrently and preserve their historic installation/build/test/upload bodies. This is a **UI, ownership and trigger simplification**, not yet a real reduction in T1 runner minutes. Each of the three jobs currently builds the entire toolchain through `make ... all` in a separate BUILD_DIR; the installations and builds overlap. The post-consolidation real Runtime run [#37887392476](https://github.com/yituanxing/minic-toolchain/actions/runs/37887392476) had jobs of roughly 32s (MiniC), 28s (MiniLD), and 28s (MiniAR); Performance run [#37887416377](https://github.com/yituanxing/minic-toolchain/actions/runs/37887416377) roughly 27s, 33s and 27s. Both runs were SUCCESS. Their distinct verdicts remain diagnostically useful; replacing them with a single fail-fast job would risk masking downstream failures. Sharing setup/artifacts is a separate, evidence-gated optimization.

Runtime M0 [#37887392381](https://github.com/yituanxing/minic-toolchain/actions/runs/37887392381) and Performance M0 [#37887416392](https://github.com/yituanxing/minic-toolchain/actions/runs/37887416392) are SUCCESS. M0 checks original full job bodies against three byte-preserved archives per branch, positive source/ELF triggers and negative Runtime-only/CI maintenance triggers. The real canonical T1 runs each reported all three owners SUCCESS. No Image or boot certificate is implied.

## Concrete overlapping job groups and dispositions

| Candidate cluster | Current entries/jobs | What repeats | What is different | Decision |
| --- | --- | --- | --- | --- |
| MiniC RV64, MiniAR, MiniLD T1 | **1 YAML / 3 jobs** per branch | `make ... all`, apt update, toolchain setup, shared MiniELF test | RV64 target behavior, archive/NM, static/dynamic linker and independent failure verdicts | **Merged YAML now; preserve 3 jobs; investigate reusable build only after measuring actual costs** |
| Linux Image graph preflight vs complete distributed Image | **2 YAMLs**, independent `graph` jobs | Linux 6.6.143 download/config, `make -n` manifest and 7-shard plan | The complete Image job additionally tests Core constant-p, then launches shards, strict replay, Image link/QEMU. Preflight is a much cheaper independent diagnostic | Factor exact graph setup into a common script; only merge entrypoints after routing distinguishes graph-only vs full-Image. **Do not trigger full Image accidentally.** |
| Init-IRQ bridge, RCU/softirq, RISC-V init codegen, timer frontier | **4 YAMLs / 4 jobs** per branch | Certified fixture/source restore, MiniC profile build, some Kbuild/QEMU setup | Object cohort, boundary/first fault, output artifact and trigger file differ | Future Runtime owner selector candidate; retain exact original YAMLs until routed QEMU and cache evidence exists. |
| MiniAS focused semantics vs A0 Gate | **2 YAMLs / 15 declared jobs** on Runtime | Some C149/native semantic support and toolchain builds | A0 exact/3536 target vs independent semantic/native oracle and cohort evidence | **Not proven redundant**. Check identical commands and output provenance before deduplicating. |
| Compiler Core shards vs full3352+Strict500 suite | **2 YAMLs / 11 jobs** per branch | Frozen corpus/build setup and shard replay | Crash/ASan and independently certified corpora vs strict500/full assembly | Keep independent T2 contracts; audit shared setup only. |
| Performance-specific experiments | **1 canonical Performance YAML / 5 jobs (4 experiments + route)** | First500/compiler performance or targeted compiler semantics setup | `top5`, GNU constant-p ICE, optimized first500, parser-scope A/B run different candidate patches and oracles | **Strong temporary-file consolidation candidates**, but source/branch/profile proof and opt-in semantics must be retained. Do not treat their success as optimized source merge. |
| MiniLD T1 vs real dynamic/Linux REL/static integration | **2 YAMLs**, distinct job roles | Linker build/test prerequisites | T1 synthetic contracts vs T3 real binary integrations | Keep separate due evidence tier. |
| MiniPP A0 vs frozen/live exact | **2 YAMLs** | MiniPP builds | Fast T1 unit tests vs expensive immutable T2/T3 corpora | Keep cheap automatic T1 separate. |

## Why workflow jobs look large in the UI

Workflow **declarations** are not queued or billed jobs. Many active YAMLs are `workflow_dispatch` opt-in, or have commit-tag `if` conditions: a push can create a workflow run whose meaningful jobs are all SKIPPED. A few owner router jobs intentionally spend a short runner invocation to avoid multi-owner QEMU. GitHub Actions UI entry clutter is not the same as runner cost, but it harms maintainability.

For a meaningful further reduction, require both properties: (1) no unique assertion lost and all manual/push modes remain reachable; (2) the new routing must not turn a cheap push into T2/T3/T4 work. Do not count archived files as active workflows. Do not collapse individual job verdicts until an aggregate failure-preserving scheme is tested.

## Next safe consolidation queue

1. **Cost first:** extract the new T1 jobs' setup, build, test elapsed time and quantify whether caching a shared binary saves meaningful billed runner minutes. Preserve independent result/status/artifacts even if setup is shared.
2. **Performance P temporary workflows:** decide which were superseded by the canonical optimized profile, then bring remaining opt-in jobs into one owner with exact branch, run tag and source-HASH checks. Expected upper bound: 4→1 Performance YAMLs, without dropping four modes.
3. **Runtime legacy focused owners:** central route by changed trigger filename, manual mode and incomplete git diff; preserve four distinct QEMU oracles and immutable cache provenance. Do not archive originals without a real tier-matched smoke/check.
4. **Image preflight:** share plan/config construction with full Image producer, but avoid associating a small manifest edit with seven full Linux shards.
5. **Stale operator docs:** update current banners and replace old active links with the new T1 canonical file. Archived names are for proofs/history only.

**Observed overtrigger after consolidation:** a no-op Python comment in `tools/ci/apply-perf-core-object-interval-onepass-v1.py` automatically started all three generic toolchain T1 jobs although their plain `make all` does not execute P15 patch scripts. The canonical T1 suite now specifically excludes `tools/ci/apply-perf-*` and `tools/ci/linux-perf-*` (Performance-only experimental helpers), while retaining `src/**`, `include/**`, actual `tools/**` implementations, `elf/**`, and test paths. M0 asserts these negative rules and that all original source/test routes remain positive. No code generation assertion or general compiler correctness test has been removed: these patch-specific edits are routed by the Performance experiment owner, not the base unpatched toolchain regressions. This changes intended trigger scope to reduce **three redundant runner jobs per P-only script edit**. Verify with a new real patch-script-only push; M0 source proof alone is insufficient.

Full Image (3352 objects, link, QEMU) remains independently uncertified at current HEAD; CI structural and T1 SUCCESS must not be reported as its replacement.

## Verified elimination of redundant P-only patch source trigger

A controlled one-file change in `tools/ci/apply-perf-core-object-interval-onepass-v1.py` first launched the canonical `top5` P experiment **and** unrelated MiniC RV64/MiniAR/MiniLD T1 jobs ([old overhead #37888314089](https://github.com/yituanxing/minic-toolchain/actions/runs/37888314089)). We subsequently excluded only pure `tools/ci/apply-perf-*` and `tools/ci/linux-perf-*` helpers from the general T1 workflow. Repeating the same exact-file change launched `top5` successfully ([#37888511372](https://github.com/yituanxing/minic-toolchain/actions/runs/37888511372)) while **no new generic T1 run was created** for the new HEAD. The target P experiment still ran and preserved a strict opt-in for expensive First500. The T1 exclusion is protected by M0; both post-fix branch M0 structural runs [Runtime #37888471362](https://github.com/yituanxing/minic-toolchain/actions/runs/37888471362) and [Performance #37888484183](https://github.com/yituanxing/minic-toolchain/actions/runs/37888484183) are SUCCESS.

**Interpretation:** prior three T1 jobs each billed roughly 25–35 seconds of runner time in focused regression runs; we no longer spend those three generic build/test invocations on P-only helper edits. This does **not** suppress tests for actual `src/**`, `include/**`, `elf/**`, `archiver/**` or `linker/**` changes.
