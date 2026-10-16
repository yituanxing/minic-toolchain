# MiniC 2064-TU phase census: scope, bias and implementation gate (2026-10-16)

## Why a full census is required
Current `perf500` first500 set is a pinned *subset* of the full Linux 6.6.143
RV64 2064-owner C universe. The 20-owner run #38901917827 is *deliberately
stratified* as 10 historical longest, 5 near-median, 5 short controls; it is
NOT a probability sample of either 500 or 2064 objects.

Recorded in the SUCCESS run #38901917827, current checked-in production
compiler source with diagnosis-only -O2 sibling, 20/20 .s SHA identical:

| Group | Objects | Instrumented MiniC total | Boolean-specialization time | Boolean share |
| --- | ---: | ---: | ---: | ---: |
| 10 hot | 10 | 48,924.97 ms | 31,369.41 ms | 64.1% |
| 5 middle | 5 | 3,184.60 ms | 1,491.93 ms | 46.8% |
| 5 light | 5 | 335.40 ms | 27.13 ms | 8.1% |
| **Selected 20** | **20** | **52,444.97 ms** | **32,888.47 ms** | **62.71%** |

The hot ten contribute 93.3% of these selected total compile milliseconds,
by construction. This **does not** show 62.71% Boolean time across Linux.
It does make Boolean-transitive a plausible high-payoff current **hot-TU**
research target, not a production optimization authorization.

Historical first500 same-runner old/new #38892740386 measured
1508.74s -> 1475.21s (500/500 identical), but its owners are not the
entire 2064, and uninstrumented 500-file A/B is not a phase census.

## CPU profiler correction
Artifact #12047031354 of #38901917827 contains valid stage timing summaries.
Its `callgrind-inclusive.txt` was **not** a valid MiniC CPU-function profile:
the prior command targeted `bin/minic` driver, which fork/execs `bin/minic-cc`,
and Callgrind reported merely 209156 instructions, mostly glibc dynamic
loader. Treat any implied CPU-function ranking from that artifact as invalid.
The existing CI was patched in `c7d5aae1` to target **bin/minic-cc directly**
and require a compiler symbol in Callgrind reports. CPU-function profiling
must be rerun; the earlier correct 20/20 stage-time capture is retained.

## Desired full, *cheap* 2064/2064 phase census
Only do this during the **next actual 2064-object native/Linux build** that
is needed for toolchain correctness or profiling. Do **not** start an
expensive second full compilation solely for statistics. Preserve seven
existing producers/jobs and the exact SHA-bound greedy owner manifest.

1. Each compiler process should produce ONE opt-in, compact summary for its
   actual `.i -> .s` invocation, using `CLOCK_MONOTONIC`/process CPU time
   and recording: owner, immutable `.i` SHA, compiler SHA, stage wall and CPU
   time (or clearly mark unavailable), clone generated/kept/pruned totals,
   and final `.s` SHA. Include integer/symbolic/Boolean direct/transitive
   with **exclusive** nonoverlapping outer-stage sums; measure unknown gap.
2. Instrument at stage boundary only. Avoid the generic
   `MINIC_BOOTSTRAP_TRACE` for a 2064-file campaign: it logs individual
   Core-lowered functions, causes huge per-function stderr overhead and is
   not a cheap census. Keep diagnostic feature **opt-in** and default off.
3. Per each of 7 workers, write a single TSV or JSONL with exactly its
   assigned owner names, independent of the execution order; do not print
   thousands of markers into Actions log. Emit min/median/P95/total per
   pass and runner-local wall/RSS as separate quantities.
4. Receiver rejects any nonidentical input/compiler/profile SHA, missing or
   duplicate owner, missing stage, negative/overlapping time, non-2064
   unique owner union, mismatched shard schedule SHA or invalid ELF/ABI.
   Keep previous native object hashes and 12/12 QEMU runtime checks.
5. Report **both** all-Linux owner-weighted CPU/wall total stage fractions
   and distribution across 2064 TUs (median, P95, hottest N). Distinguish
   stage-subtotal from true total process time and seven-runner critical
   path. Compare the same compiled inputs and architecture.
6. For top expensive 5-10 TUs chosen *from the complete census*, run
   `perf` or Callgrind on **minic-cc**, not its `minic` wrapper. Keep
   Callgrind instruction counts separate from CPU time. Then count hot-loop
   iterations/hit ratios before changing source algorithms.

## What is already complete versus pending
- DONE: two previous production reachability/cache changes checked by
  Linux #38887211130 2064/2064 and QEMU P1 12/12.
- DONE: #38901917827 20/20 exact .s and stratified phase diagnostics;
  Boolean aggregate 62.71% on SELECTED 20 only.
- NOT DONE: validated CPU function attribution (Callgrind driver mistake).
- NOT DONE: full 2064 current pass-time census and its performance impact.
- NEEDS FIXED/REVERIFIED: new greedy seven-shard #38898364463 producer
  7/7 success and exact object merge, receiver failed before QEMU due native
  compat-vDSO provenance guard; #244fbf1b repairs the guard but new P1 still
  unverified. Never conflate with a compiler correctness failure.

Do not promote a new algorithm from the selected 20's stage percentages.
Use the new whole-universe measurements to choose the next performance work.
