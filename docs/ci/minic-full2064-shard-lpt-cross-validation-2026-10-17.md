# Full 2064 Linux owner: shard load-balance cross-run validation

Date: 2026-10-17. **Read-only, offline counterfactual, NOT an actual speedup.**
Production `src/` unchanged. No new Linux compilation was launched for this study.

## Genuine source data

- Training/holdout #1: [full GNU-PP → MiniC → GNU-AS/QEMU P1 #39023546906](https://github.com/yituanxing/minic-toolchain/actions/runs/39023546906), SUCCESS.
- Training/holdout #2: [new fresh full2064, no historical object cache, #39033023656](https://github.com/yituanxing/minic-toolchain/actions/runs/39033023656), SUCCESS.
- Downloaded **all seven shard artifacts from BOTH runs** and parsed their **original `stage-probe/stages.tsv`** with each target's owner name; 2064/2064 unique matching owner identities, and 2064/2064 valid successful `gcc_E`, `minic`, and `gnu_as` phase observations in **each** run. Each target uses its own last successful original Kbuild stage call to account for duplicates/helpers, not abstract TU input size.
- Recorded full Kbuild shard wall in the fresh run (shards 0..6): **71.886, 71.637, 58.669, 46.619, 55.490, 74.640, 45.796 s**; most critical observed shard **74.640 s**. Full cross-runner span **94.325 s**, end-to-end GitHub Actions **398 s** (all include different overhead). Kbuild `-j6` on each of seven independent GitHub runners.
- Earlier `PERF-005` in `docs/ci/linux-performance-causal-investigation-v1.md` **already designed LPT greedy scheduling**, and `tools/ci/linux_minipp_seven_balanced_plan_v1.py` plus its SHA-bound owner-plan and merge proof already exist for the **different native PP/AS pipeline**. Do not rebuild that algorithm as a novel solution; any full GNU path integration must adapt its producer, receiver, artifact and SHA contracts.

## Algorithm (offline only)

For each owner i, use the *measured* last successful phase process-wall time:
`w_i = wall(GNU GCC -E) + wall(MiniC -S) + wall(GNU AS)`.
Sort descending by `w_i`; assign each object to the currently least-loaded of seven buckets with capacity **295** owners/bucket. This is a deterministic LPT greedy *model* (O(N log N + 7N)), total owner set unchanged. The 294-object shard identity may differ from current canonical shard 6 in offline simulation; production must version/pin the mapping and verify unique union before use.

Because seven GitHub runners have heterogeneous CPU performance and measurement noise, **do not validate against the same-run data that generated the weights**. Cross-evaluate each historical schedule on the *other* full run's 2064 owner times:

| Evaluated run | Current 7-way stride worst weighted shard | LPT trained on other run: worst weighted shard | Predicted reduction in worst per-shard summed process wall |
| --- | ---: | ---: | ---: |
| Fresh #39033023656, trained on #39023546906 | **351,277ms** | **287,538ms** | **18.14%** |
| Older #39023546906, trained on fresh #39033023656 | **357,700ms** | **301,296ms** | **15.77%** |

For the MiniC child stage alone, cross-run holdout estimates:
- Old-trained → fresh holdout worst: **207,888 → 173,134ms** (**16.72%**).
- Fresh-trained → old holdout worst: **212,213 → 179,541ms** (**15.40%**).

Within fresh run, the stride shard-weight distribution totals (ms) **[343994, 332091, 266324, 215893, 254581, 351277, 209426]**, worst/average **1.2459**. LPT **trained only on the older run** yields on fresh holdout **[280370, 287538, 282107, 283688, 280367, 284121, 275395]**, worst/average **1.01985**. Full-owner cost Pearson correlations between the two runs: **0.8483** for total stage process wall and **0.8341** for MiniC process wall.

**Huge measurement caveat:** all these numbers are the sum of **process wall times while processes concurrently run** on hosts with different CPU power and a six-way Kbuild scheduler. They are *not* the measured parallel shard wall, even though historical weighting correlates with cost. For a rough illustration only, normalizing each fresh shard’s actual elapsed by its original summed stage-wall and applying it to the old-trained holdout model yields a **hypothetical 62.15s maximum shard**, compared to actually measured **74.64s**. This 62.15s is **not observed, not a P50, and not a committed acceptance target**. It can change when the tasks are actually moved and their host CPU/scheduling interaction changes.

## How to measure an actual speedup

1. Reuse the existing seven CI jobs; first do not change compiler sources, toolchain, Linux .config, flags, frozen 2064 owners, or scheduling `-j6`. Compare **old stride versus owner-weighted** schedules. Plan derived ONLY from earlier measurements (no live-data leak), has canonical versioned SHA, count and unique-union gates. The old native LPT route has useful compatible validation patterns, but it is **not identical** to this GNU-based 2064 Kbuild route.
2. Each true fresh run must reproduce original GNU-PP → MiniC → GNU-AS for **2064/2064**, compile each unique owner, check ELF/ABI and object-manifest SHA, merge exactly one candidate per owner, run GNU linker and QEMU P1 12/12. Changing both producer+merger is required; never reuse an old shard's object cache under a different owner map.
3. Collect **measured** (a) per-shard Kbuild compile elapsed, (b) actual cross-runner interval earliest compile start → latest finish, (c) whole workflow elapsed, (d) total MiniC/GNU-E/AS child CPU and Wall, (e) peak per-compiler RSS and optionally real concurrent cgroup/PSS. Compare critical path `max(shard compile wall)` rather than `sum(shard times)`, and report warm-cache bias and Runner heterogeneity. Repeat/reverse order when feasible.
4. Schedule acceleration only reduces critical path and waiting; with unchanged sources it does **not** lower total CPU work. For source-level compiler optimization run same-runner, same-corpus paired `.i → .s` A/B before all 2064+QEMU, and record total CPU + output bytes/semantics.
5. Do not mechanically claim **18.14% real build speedup** or a 398s full-Workflow speedup from this offline analysis.

**Decision:** strong, independently validated *candidate to run an actual seven-shard schedule comparison*, not a new CPU algorithm result. No existing source modification or CI run from this offline schedule evaluation.
