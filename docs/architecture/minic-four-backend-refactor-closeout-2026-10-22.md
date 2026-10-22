# MiniC four-backend refactor: engineering closure and evidence (2026-10-22)

## Decision: close the structural refactor, retain the performance backlog

This phase reorganized **only MiniC** across x86-64, RV64, AArch64 and
LoongArch64. It explicitly did not redesign MiniAS, MiniLD or MiniPP. The
current Core→Backend emission seam is sufficient for the verified workloads;
do not introduce a Machine IR, new target DSL or generic physical-register
allocator without a concrete consumer and paired regression evidence.

An architecture review is *not* a correctness or performance claim by itself.
The source-of-truth design boundaries and fifth-ISA checklist are in
[`docs/minic-backend-architecture.md`](../minic-backend-architecture.md).
Keep the separate correctness gate, original GNU/QEMU program oracles and
performance measurements as distinct pieces of evidence.

## Functional and architecture validation

The preceding source/architecture closure used PRs #636–#646:

- SysV x86-64 argument plan: one incoming/outgoing location owner.
- RV64 Core admission/ABI planning: no per-instruction redundant admission.
- RV64 object liveness and slot reuse: function/frame lifetime, no global
  cached Core pointers.
- RV64 FP and overflow lowering separated from scalar integer ALU.
- Direct AArch64/LoongArch64 instruction-family and residual Core operand
  shapes shared; ISA machine operations and ABI/register policies left native.
- Direct LP64 scalar normalization: one function per ISA called directly
  by the FP, integer, ABI and residual machine-family owners.
- `check-minic-target-boundaries.sh` enforces those specific contracts and
  rejects reintroduction of the old callbacks or RV64 global liveness index.

Validated current acceptance prior to this documentation change:

| Evidence | SHA / run | Result |
|---|---|---|
| MiniC strict `-Werror`, architectural invariants, x86 native ABI | `f37e73f6`, Actions [#39805450892](https://github.com/yituanxing/minic-toolchain/actions/runs/39805450892) | SUCCESS |
| Focused RV64 / new ISA original-source GNU/QEMU acceptance | `f37e73f6`, Actions [#39805450922](https://github.com/yituanxing/minic-toolchain/actions/runs/39805450922) | SUCCESS (20 CI steps) |
| Preceding four-backend closure acceptance | `de85f3c0`, Actions [#39798393605](https://github.com/yituanxing/minic-toolchain/actions/runs/39798393605) | SUCCESS |
| Paired four-ISA original C source performance + x86 runtime differential | `417ad6ad`, Actions [#39808316848](https://github.com/yituanxing/minic-toolchain/actions/runs/39808316848) | SUCCESS |

The new paired benchmark separately builds the baseline and current MiniC
with `-Werror`. The baseline is fixed at
`c638ba10a55533c6332d204fc6eb6c086ed7b67d` (immediately before
PR #636, **not before all compiler restructuring**). The candidate is
`417ad6ad2893ab901144f48a6aacde24cf59735f`. Both receive **one
identical GCC-preprocessed original tiny-AES-128 ECB C input**. Warmups are
excluded; each ISA is sampled four times per compiler in alternating
before/after order. Whole-compiler binaries, input, emitted assembly SHA-256
and wall/user/sys CPU measurements are recorded in the Actions JSON artifact.

### Measured paired single-TU results

| Target | Baseline wall median | Candidate wall median | Measured ratio | Assembly SHA equal? |
|---|---:|---:|---:|:---:|
| x86-64 | 9.951 ms | 9.982 ms | 0.9968× | Yes |
| RV64 | 11.348 ms | 11.960 ms | 0.9487× | Yes |
| AArch64 | 14.957 ms | 14.790 ms | 1.0113× | Yes |
| LoongArch64 | 10.470 ms | 10.471 ms | 0.9999× | Yes |

The old and new emitted assembly is **byte-for-byte identical for all four
ISA compilations** of this one fixture. Assembled x86-64 executables exit 0
with identical observable stdout/stderr SHA-256. This is meaningful
non-regression evidence for the tested source, not proof of every Core
opcode's behavior.

**Interpretation**: these are approximately 10–15 ms hosted-runner
compilations, so fluctuations as small as 0.1–0.6 ms are readily explained
by scheduling and noise. The RV64 0.9487× ratio is a **follow-up
measurement candidate**, not evidence that the refactor caused a 5% systemic
regression. Do not claim an overall speedup or latency regression from one TU.

### What this experiment does *not* prove

- It does not compare all 2064 Linux kernel translation units, the 500-file
  cohort, or a broad compiler performance distribution.
- It does not compare source preprocessing or GNU assembler/linker times.
- It is not a pinned QEMU runtime regression for all four generated binaries
  (that is owned by the independent focused CI).
- It is not a compiler-performance comparison against the beginning of the
  entire #598-era series: it brackets PR #636 onward.
- Historical 2064-TU A/B numbers documented in
  `docs/ci/minic-full-linux-2064-performance-evidence-2026-10-15.md`
  apply to **older, different compiler SHA pairs**; they must not be
  attributed to this structural refactor.

## Final acceptance and deferrals

**Accepted for architectural closure**: Core semantics and target ISA
emission have explicit ownership; the existing AArch64/LoongArch64 direct
backend shares only semantically identical facts, while SysV x86-64 and
RV64 preserve their independent ABI and lifecycle contracts. Strict native
and cross-ISA original-source suites passed on the cited immutable SHAs.
The exact current integration SHA should separately pass the focused suite
before a release tag or cross-branch promotion is made.

**Deferred, with explicit reasons**:

1. **Broad compiler performance**: compare *current vs pinned pre-refactor*
   using a representative frozen original-source sample (cJSON, SQLite,
   BusyBox and Linux), A/B alternating repeats, CPU + RSS, and optionally
   byte-identical assembly. Do not run a 2064-file seven-runner suite simply
   to obtain a cosmetic benchmark headline.
2. **Machine IR**: add only when a concrete virtual-register allocator,
   selection scheduler or copy-elimination pass can quantify a benefit.
3. **Code quality**: benchmark emitted instruction counts, stack frames and
   runtime separately from frontend/compiler throughput.
4. **New ISA**: prove the design by implementing one new backend using
   existing Core semantic facts and adding ABI-specific machine operations,
   not by prematurely making the four current backends emulate each other.

**Stop rule**: normal feature development resumes; structural refactoring
reopens only when a measured regression, a new ISA integration bottleneck,
or an actual duplicated semantic computation demonstrates the need.
