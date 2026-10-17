# MiniC AST Memory Shape Census (2026-10-17)

## Scope / reproducibility

- Source baseline: runtime-certified AST array-owner one-pass, `src/frontend/ast_verifier.c` committed in `f2bc0fcd`; 2064 Linux C + GNU link + QEMU P1 certified in [#39000339623](https://github.com/yituanxing/minic-toolchain/actions/runs/39000339623).
- Independent new *read-only*, temporary compiler-source probe: `tools/ci/apply-perf-ast-shape-census-v1.py`, invoked in the existing **perf500** workflow job by the opt-in tag `[minic-ast-shape-census]`. Original compiler and instrumented release compiler receive the **same frozen GNU Linux 6.6.143 .i**, **7/7 `.s` files byte-identical**, CI [#39001510651](https://github.com/yituanxing/minic-toolchain/actions/runs/39001510651) SUCCESS. No production source algorithm or AST object format changed.
- An earlier `tools/ci/apply-perf-ast-footprint-trace-v1.py` and `linux-perf-ast-footprint-v1.sh` **already existed** for different older cohorts. This census **extends** historical shape profiling with post-2026-10-17 source identity, *seven new same-cohort frozen owners*, parsed/final counters, CALL-argument histogram, and separately measured original-versus-probe peak RSS. Do not confuse this instrumentation with an optimization candidate, do not reintroduce a new standalone workflow.
- Measurements are *per compiler process*. Nominal capacity × sizeof is not true DRAM bandwidth; allocator virtual reserves are not all pages necessarily resident; `/usr/bin/time %M` is OS peak RSS and not an incremental memory saving measurement. Sum across TUs describes a study cohort, **not** concurrent CI-runner peak RSS.

## Current actual AST layout

At this host ABI: `sizeof(MinicExpression)=680` bytes, `sizeof(MinicFunction)=6536` bytes, `sizeof(MinicFunctionType)=5216` bytes. The Expression CALL union reserves **64 × 8 = 512 bytes of parameter IDs** even for non-CALL expressions. Functions embed **64 parameter types** plus specialization fact arrays. This is why the initial/structural hypothesis from [whole-path audit](minic-i-to-s-whole-path-complexity-audit-2026-10-15.md) deserves careful follow-up.

| Frozen real Linux owner | final Expression count / capacity | CALLs (% all expressions) | CALLs >8 args | expression nominal capacity | function nominal capacity | original compiler max RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| fs/ext4/fsync.o | 116,462 / 131,072 | 7,914 (6.80%) | 0 | 85 MiB | 102.12 MiB | 242.9 MiB |
| kernel/bpf/btf.o | 219,953 / 262,144 | 12,773 (5.81%) | 14 | 170 MiB | 102.12 MiB | 418.4 MiB |
| kernel/events/core.o | 208,812 / 262,144 | 12,148 (5.82%) | 3 | 170 MiB | 102.12 MiB | 395.2 MiB |
| mm/memcontrol.o | 202,455 / 262,144 | 12,108 (5.98%) | 6 | 170 MiB | 102.12 MiB | 386.6 MiB |
| fs/ext4/super.o | 144,179 / 262,144 | 10,073 (6.99%) | 1 | 170 MiB | 102.12 MiB | 294.1 MiB |
| mm/hugetlb.o | 132,943 / 262,144 | 9,439 (7.10%) | 2 | 170 MiB | 102.12 MiB | 260.7 MiB |
| mm/vmscan.o | 133,572 / 262,144 | 9,117 (6.83%) | 0 | 170 MiB | 102.12 MiB | 271.0 MiB |

Seven-input cohort: 1,158,376 expression records, **73,572 CALL records (6.35%)**, **26 CALLs with more than eight arguments**, median per-input CALL ratio **6.795%**, and 1,703,936 nominal Expression slots across separate invocations. All seven P1-certified production compiler .s matched the instrumented copy.

**Interpretation:** It is technically warranted to investigate a smaller Expression payload, because **93.65%** of stored expressions are *not CALL*, while each nominal Expression slot reserves space for 64 arguments; the actual copy/memory costs need A/B proof before claiming any speed gain. The per-TU 102 MiB Function-capacity array independently motivates auditing function-side specialization facts, but **do not** redesign both giant structs at once.

## Candidate ordering / do-not-repeat rules

**Card: `ast-expression-compact-call-payload` — `needs-design-and-safe-prototype`, not yet implemented.**
- Goal: preserve all 64 arguments and evaluation order; avoid embedding a 64-ID table in every Expression. Potential design: immutable or exclusively-owned per-CALL argument payload allocated in a per-Program arena, with stable indexing and safe cloning. Need to examine all production use sites of `value.call.arguments` (parser, AST traversal/normalization, verifier, specialization, compiler), all struct-by-value copies, mutation, cloning, ownership, growth and destruction. Do not replace the fixed array with a bare malloc pointer; that would introduce aliasing, dangling-pointer, and double-free risks.
- Prototype only a **single** representation change behind a bench-only patch in a disposable checkout, not a simultaneous rewiring of Function representation. Confirm tests for 0, 1, 2, 8, 9, 63, and 64 arguments, parser failure cleanup, indirect/variadic calls, GNU cleanup, specialization clones and in-place arg rewrite, and 64-limit rejection.
- First validate exact AST contract and basic compilation/cross tool ABI. Require RSS change, user CPU time, compile wall on at least two large and two tiny frozen `.i`, then **byte-for-byte `.s`**. Stop if it regresses CPU wall or worsens memory allocation frequency without corresponding benefit. Only then rotated independent owners → first500 old/new → 7×2064+GNU link+QEMU P1. The 7-owner current census **does not authorize** a production representation edit yet.
- This is fundamentally distinct from **PERF-033 rejected change** (2×→1.5× array capacity growth), Parser switch snapshot, Parser function/global hashes, and AST array semantic-owner index. Changing only growth factor is prohibited without a new cost model; in particular **do not retry PERF-033** under the "compact AST" label.
  
**Card: `ast-function-cold-parameter-facts` — `queued-after-expression`.**
- `MinicFunction` is 6536 bytes and up to 16,384 slots per large input, nominal ~102 MiB even when the real function count and average argument count are much smaller. Potentially separate *optional specialization facts* and/or parameter lists for functions using them; first audit clone ownership and referential semantics, not just memory sizing. An independent alternative if Expression refactor is too invasive.
  
**Card: `layout-repeated-record-work` — `needs current CPU evidence`.**
- `src/target/data_layout.c` contains nested record traversals; prior audit notes RV64 Codegen repeats some per-object type-layout work. Need actual layout calls/CPU cost under latest AST+other optimized source before creating yet another memoization table. Do not retry previously rejected P14/P15 by renaming them.

## Result after candidate promotion (2026-10-17)

The first compact-Expression experiment is now **integrated and runtime-certified**. It **keeps the 64 inline CALL argument slots and full value-copy semantics**, but stores 32-bit IDs instead of size_t IDs, with explicit no-truncation guards. Thus the slot table alone declines from 512 to 256 bytes. No new per-CALL mallocs, no changes to the historically rejected 2x→1.5x array growth experiment.

| Check | Evidence |
| --- | --- |
| Four Linux TU paired | [CI #39002084966](https://github.com/yituanxing/minic-toolchain/actions/runs/39002084966), 4/4 exact .s, 1.04217x compile speedup, one-sample peak RSS reduction ~22.5%–26.4% on fsync/BTF/memcontrol |
| first500 gate | [CI #39002211001](https://github.com/yituanxing/minic-toolchain/actions/runs/39002211001), 500/500 byte-identical .s, 289603.901→269198.125ms, **1.07580x** total speedup, 250/250 balanced first-order |
| CALL arg boundary & AST contract | [CI #39002498179](https://github.com/yituanxing/minic-toolchain/actions/runs/39002498179) SUCCESS; 0/1/2/8/9/63/64, variadic and GNU cleanup agree; 65 arguments rejected; contract PASS |
| Production source | [Commit 5cb252e9](https://github.com/yituanxing/minic-toolchain/commit/5cb252e918112337d4e281267a0349ad490a4b55), six frontend source files |
| Focused T1 | [CI #39002613399](https://github.com/yituanxing/minic-toolchain/actions/runs/39002613399) SUCCESS, 8 independent checks |
| Runtime | [CI #39002638516](https://github.com/yituanxing/minic-toolchain/actions/runs/39002638516) SUCCESS, 7/7 shard, 2064/2064 C, GNU link, QEMU P1 12/12 |

The initial full-run attempt #39002613400 failed on the **deliberate source-SHA identity gate** due to the previous ast.h pin, not due to a Linux compiler failure. It was corrected in the manifest, and the actual rerun passed.

Regression triage [CI #39002748529](https://github.com/yituanxing/minic-toolchain/actions/runs/39002748529) collected 9 real owners (7 initially slower + 2 positive controls), 8 old/8 new runs each, 9/9 matching .s, **0 reproduced wall-and-CPU regressions** and **all 9 median wall-time improvements**. Nine JSON reports are preserved in artifact 12089693082. The workflow still shows FAILURE because the final evidence directory had not been created before tee wrote its summary; this workflow-only mistake is fixed in commit 6665be9f; the prior data was not silently relabeled as an overall CI success or needlessly rerun.

**Close the candidate: packed inline CALL ID is integrated / runtime-certified / do-not-repeat.** Preserve the UINT32_MAX upper bound and require a new performance/cost model before attempting more external CALL-argument allocations. Future independent candidates include the 6536-byte Function cold parameter/fact representation and layout rescanning, each subject to new CPU and RSS evidence.

## How many iterations remain?

No defensible exact count of successful optimizations. The next **5–8 disciplined candidate/screening rounds** are plausible from the currently *unresolved* structures: Expression footprint, Function cold arrays, layout, new phase attribution after memory changes, and remaining non-repeated Symbolic/Parser/Codegen algorithms. That number is an **exploration budget, not a guarantee of 5–8 accepted patches or of cumulative speedup**. Expect most micro-candidates to fail promotion gates as before.

**Stop/rotation rule:** Any proposal with real 500-TU gain <~1–2% or unstable CPU/RSS trade-offs is not promoted by default; promote modest wins only with unusually strong multi-scale evidence and runtime safety. If 3 consecutive well-attributed distinct candidates fail expected impact, re-profile the full current source instead of blindly drafting more hashes/indices. Archive negative results and never reopen without an updated baseline plus changed cost model.

## Second space optimization: MinicType layout (2026-10-17)

This is a **distinct candidate** from packed CALL IDs. Keeping `MinicType` member types and all four 64-bit ID fields unchanged, reordering 1/4-byte flags and enums ahead of the 64-bit fields eliminates member alignment holes. The single production change is `src/frontend/type.h` [a6186876](https://github.com/yituanxing/minic-toolchain/commit/a6186876c684b0cbe945b77dc5c96ebce6f6d6fb).

Real host ABI `sizeof` measured on same runner in original and candidate arms:
- `MinicType`: 80 → **72 bytes**
- `MinicFunction`: 6536 → **6016 bytes** (520 bytes per value/allocated capacity slot)
- `MinicFunctionType`: 5216 → **4696 bytes**
- `MinicExpression` after previously packed CALL IDs: 424 → **416 bytes**

Five first-stage Linux Kbuild owner A/B [#39005831148](https://github.com/yituanxing/minic-toolchain/actions/runs/39005831148) SUCCESS, `.s` 5/5 identical, `make check-ast-contract` PASS. Sum of five independent per-owner wall medians 3649.556→3555.644 ms (1.02641x); peak RSS declined in all five once-run samples, e.g. BTF 321656→306184 KiB, FSYNC 189368→180524 KiB.

500-owner same-runner paired test [#39005940554](https://github.com/yituanxing/minic-toolchain/actions/runs/39005940554) SUCCESS, all 500 `.s` exactly identical; balanced 4-worker old total **348126.483ms**, candidate total **341344.660ms**, **1.01987x speedup / 1.948% cumulative time reduction**. Single-pass 107 of 500 candidate owner wall times looked worse. This is a *one-iteration* regression count, not per-owner CPU certainty. Ten owners selected from eight worst single-shot regressions plus two positive controls were each retested eight times per arm using the same paired CPU/user/sys/MAD tool [#39006242085](https://github.com/yituanxing/minic-toolchain/actions/runs/39006242085) SUCCESS, all 10 owner median wall times improved, **0/10 reproduced dual wall+CPU slowdowns**. The other 99 single-pass outliers were not individually retested; preserve them in first500 differential artifact.

Full production certification [#39006469186](https://github.com/yituanxing/minic-toolchain/actions/runs/39006469186) **SUCCESS**, seven shards 2064/2064 C, GNU link, QEMU P1 12 syscall Python-era oracle PASS; focused suite [#39006469220](https://github.com/yituanxing/minic-toolchain/actions/runs/39006469220) SUCCESS with 8 independent checks.

**Decision: `minic-type-field-layout-72-v1 = integrated / runtime-certified / do-not-repeat`.** This is accepted near the usual minimum gain threshold because it preserves all field types/semantics, structurally saves 8 bytes per MinicType across the full compiler, yields observable RSS gains, and survives 500 files + 10-owner regression rechecks + 2064/QEMU. Next candidates should be chosen from a *new current-baseline CPU/RSS profile* rather than attempting another reorder of the same fields; notably Function parameter/fact storage remains large even after this change.
### New Function occupancy triage, post-MinicType layout

**Current-source census [#39007018275](https://github.com/yituanxing/minic-toolchain/actions/runs/39007018275) SUCCESS**: same seven real SHA-frozen Linux owners, diagnostic only, **7/7 assembly exactly identical**. This is after both packed CALL IDs and 72-byte MinicType member layout passed independent complete 2064/QEMU P1 runtime certification. Current measured C structure sizes are MinicExpression **416B**, MinicFunction **6016B**, MinicFunctionType **4696B**, not theoretical projections.

Across seven **separate** TU invocations (not concurrent peaks):
- 85,327 final Function records / 114,688 allocated Function array slots. Histogram: **8,735 functions 0 parameters**, **34,567 with 1**, **23,050 with 2**, **15,655 with 3–4**, **3,241 with 5–8**, and **79 with >=9**.
- **66,352 / 85,327 = 77.76%** of Functions have 0–2 parameters; 150,999 live parameter type slots total; mean **1.770 args per Function**, while each Function still reserves 64 fixed MinicType entries. This is a structural storage cost measurement, **not evidence for a speculative dynamic-pointer speedup**.
- **2,903 Functions** have is_integer_specialization set (**3.40%**). The counts of set per-parameter specialization fact slots are 1,000 symbolic and 120 Boolean. Counts of integer-known fact slots **were not recorded**: cannot conclude that all other Functions have no specialization facts.
- Nominal Function array capacity is **94 MiB in each large 16,384-slot TU**, **658 MiB added over seven different invocations** (do not treat as concurrent RSS). Original compiler peak RSS KiB: fsync 181,600, BTF 306,764, events core 288,668, memcontrol 282,456, ext4 super 220,992, hugetlb 193,356, vmscan 199,912.

**Next new candidate (not implemented):** analyze all Function parameter-type consumers, by-value copies, specialization clone behavior, owner lifetime and OOM. A compact Function with per-Program side storage may reduce wasted inline 64 types, but directly changing a fixed array into a malloc pointer can create alias/dangling-pointer problems and slower random memory access. Do not promote any new representation without single-factor local old/new user+wall+RSS, exact output semantics, then first500 and 2064+QEMU certification. Do not repackage the already-integrated type-layout or packed CALL ID ideas as new optimizations.


## Third space optimization: exact-owned Function parameters (2026-10-17)

The post-type-layout 7-TU census [#39007018275](https://github.com/yituanxing/minic-toolchain/actions/runs/39007018275) found 85,327 Functions, mean 1.770 parameters per Function, 77.76% with <=2 while each Function reserved 64 MinicType slots. A direct "four inline + overflow pointer" design is **not** safe without relocation/snapshot surgery: by-value Function copies and relocatable Program::functions storage can create dangling pointers into a Function's inline slots. Instead, a focused temporary A/B first evaluated a **private exact-length Function parameter allocation** with one free per Function in Program destruction, clone deep-copy via existing signature setter and no changes to FunctionType or specialization fact arrays.

- [5 frozen owner A/B #39008785116](https://github.com/yituanxing/minic-toolchain/actions/runs/39008785116) SUCCESS: real sizeof(MinicFunction) **6016→1416 bytes**; 5/5 byte-identical assembly and AST contract PASS; sum of median per-owner compilation times 3885.191→3472.217ms (**1.11894x**). One-shot peak RSS KiB: fsync 182372→137688 (about -24.5%), BTF 306592→243728 (~-20.5%), events-core 289216→231772 (~-19.9%), memcontrol 285756→227712 (~-20.3%).
- [Full frozen first500 #39008901495](https://github.com/yituanxing/minic-toolchain/actions/runs/39008901495) SUCCESS: **500/500 identical .s**, 4-worker balanced-order, compile-duration sum **336058.147→316391.869ms**, **1.06216x / 5.85% cumulative time reduction**; single-file median speedup 1.06065x. Original first500 recorded 20/500 single-run apparent slowdowns; most likely scheduling noise, but should not be assumed away.
- [10 repeated owner triage #39009160280](https://github.com/yituanxing/minic-toolchain/actions/runs/39009160280) SUCCESS: 8 largest single-run regressions + BTF/fsync controls, each 8 old/8 new paired CPU/user/sys/MAD measurements: 10/10 .s byte-identical, 10/10 wall medians improved and **0 reproduced dual CPU+wall regressions**. The other 12 one-run slower owners were not individually rechecked.
- Integrated production [commit d244df7d](https://github.com/yituanxing/minic-toolchain/commit/d244df7df25dda8ef282280f6540bef9a40eff29) changes only src/frontend/ast.h and src/frontend/ast.c and synchronizes header SHA pin. It retains all 64-parameter semantics, retains all specialization fact slots and preserves compile-stage value-copy read patterns by borrowing independently owned heap memory. On allocation failure signature modification aborts before mutating an existing Function.
- [Dedicated Function ownership contract #39009503446](https://github.com/yituanxing/minic-toolchain/actions/runs/39009503446) SUCCESS: tests Function-header realloc stability, borrowed-by-value snapshots, owned clone separation, signature redefinition/aliasing, invalid-signature rollback, all 64 parameters, zero arguments. Reuses check-ast-contract and the existing perf500 job.
- [Focused T1 #39009485250](https://github.com/yituanxing/minic-toolchain/actions/runs/39009485250) SUCCESS. [Full Linux #39009342014](https://github.com/yituanxing/minic-toolchain/actions/runs/39009342014) **SUCCESS**: 7/7 C shard, 2064/2064 C, GNU Link, QEMU P1 12-syscall oracle PASS. This is GCC preprocessor/MiniC C stage/GNU assembler-linker, NOT complete Mini-only toolchain integration.

**Final status: integrated / runtime-certified / do-not-repeat.** It is a distinct optimization from packed CALL argument IDs and MinicType padding reorder. Improvements across different run baselines are not directly additive. The new measured baseline should be re-profiled before opening any separate FunctionType packing, arena, or specialization fact storage candidate: fewer bytes by themselves do not justify additional pointer indirection or allocator changes.
