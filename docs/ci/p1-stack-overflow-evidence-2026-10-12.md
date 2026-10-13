# MiniC Linux 6.6.143 P1 stack overflow: measured evidence (2026-10-12)

## Reproducible evidence

The frozen all-MiniC Image `d1a0f9f67cb0dcb6ee6229abed2409e1c454de5074f6e8a752d3dfe004c85e98` failed P1 all three times. With **the same exact mixed Image** `e02b6335582a6083b6d05080caed0ff09e08e084526cfae96468e050877f9490`, replacing only `net/ipv6/ip6_output.o` with GCC produced PASS, PASS, FAIL. Thus a one-owner GCC result is **not** a stable rescue. Runs: 38224538679.

QEMU's actual RISC-V GDB stub (run 38226446563) measured the following chronological stack pointers in PID 39 on the failing IPv6 TCP path. The 16 KiB task stack is `ff200000001c8000..ff200000001cc000`.

| Entry function | SP | Bytes above stack bottom |
| --- | --- | ---: |
| `tcp_v6_connect` | `ff200000001cb870` | 14448 |
| `__tcp_transmit_skb` | `ff200000001cb280` | 12928 |
| `ip6_xmit` | `ff200000001cabb0` | 11184 |
| `ip6_output` | `ff200000001ca1a0` | 8608 |
| `ip6_finish_output2` | `ff200000001c9bb0` | 7088 |

An exception then reported the task SP at `ff200000001c8060`, **only 96 bytes from stack bottom**. These measurements are precise entry/exception samples, not a complete call graph, and do not alone prove which backend optimization is safe.

Runtime profile frame analysis (run 38223454312): `ip6_finish_output2` 4064 B (SSA slots 72 B), `ip6_xmit` 2288 B (SSA 72 B). The dominant frame footprint is not SSA storage.

## Rejected experiment

Run 38227315248 reenabled **cross-basic-block CoreObject stack-slot reuse** for `net/ipv6/ip6_output.o` as a strictly opt-in one-owner experiment. It reduced `ip6_finish_output2` from 4064 B to 1872 B, `ip6_xmit` from 2288 B to 1152 B; however the identical Image FAILED P1 all three times, often with the SP far **outside** the task stack (`ff1fffffffffffe0`). The 2026-10-01 cross-block safety hotfix must remain in force. **Do not promote the unsafe slot reuse.** This document records the failed hypothesis so it is not retried without new proof.

## Next controlled step

**Confirmed mixed rescue:** CI **#38228441365** restored the authenticated frozen 2064-object universe, replaced only `net/ipv6/ip6_output.o`, `net/ipv6/tcp_ipv6.o`, `net/ipv4/tcp_output.o`, and `net/core/dev.o` with GCC objects, then GNU linked a single Image of SHA-256 `c6c3456b59fcd4ce3dde6ff0e44e0fe98a5670b657500a8ad1564760537f23d0`. The same Image passed the full P1 Python-era 12-syscall contract on **three consecutive independent QEMU boots** (PASS/PASS/PASS). The all-2064-MiniC control Image failed P1 in that job. This establishes a reproducible **2060-MiniC + 4-GCC object workaround**, **not** a fully MiniC-certified compiler.

Next reduce the 4-GCC rescue set by controlled A/B before deciding where to optimize safe CoreObject lifetime/stack allocation. An unverified or unsafe cross-block stack-coloring shortcut must never be promoted merely because it makes one P1 run pass.

## Exact four-way 3-GCC minimization — completed

CI **#38237171824**: every row below uses one unique SHA-pinned Image
booted independently three times. The all-MiniC (2064) control also FAILed
in the same run. Every other C owner outside the three GCC replacements
remained an attested, frozen MiniC object.

| Restored MiniC object | 3 remaining GCC owners | P1 results |
| --- | --- | --- |
| `net/ipv6/ip6_output.o` | `tcp_ipv6.o`, `tcp_output.o`, `dev.o` | **FAIL / FAIL / FAIL** |
| `net/ipv6/tcp_ipv6.o` | `ip6_output.o`, `tcp_output.o`, `dev.o` | PASS / PASS / PASS |
| `net/ipv4/tcp_output.o` | `ip6_output.o`, `tcp_ipv6.o`, `dev.o` | PASS / PASS / PASS |
| `net/core/dev.o` | `ip6_output.o`, `tcp_ipv6.o`, `tcp_output.o` | PASS / PASS / PASS |

The failed three-GCC Image, when MiniC compiled `ip6_output.o`, sometimes
panicked with SP around `0xff1fffffffffffe0`, far below the expected task
stack. **The evidence shows `ip6_output.o` is necessary to keep as GCC in
these tested three-GCC configurations, not that it necessarily contains the
sole compiler bug.** The other three owner swaps are each individually
unnecessary within the original four-object workaround, and may still
interact with each other.

The next exact experiment tests the three *two*-GCC subsets, each retaining
`net/ipv6/ip6_output.o` and one of
`tcp_ipv6.o`, `tcp_output.o`, `dev.o`. Each distinct Image must PASS
P1 three times, with object/assembly/link provenance unchanged, before it
can be treated as the smaller workaround.


## Source-level analysis while two-GCC experiment runs

Exact Linux 6.6.143 source inspected:
`net/ipv6/ip6_output.c` in `gregkh/linux`, tag `v6.6.143`.
This is **source inspection**, not proof that the C source is wrong.
The failing `ip6_output.o` contains a number of C functions, including:

- `ip6_finish_output2` (source lines 60–144): early header expansion
  (72–82), multicast/loopback with an `NF_HOOK` call (84–115),
  light-weight tunnel diversion (117–122), and neighbor lookup/output
  (126–143); its deepest outgoing edge is the `neigh_output` path
  at line 141. The routine itself declares only a handful of C locals,
  yet the compiler IR frame trace counted **1101 CoreObjects** and a
  **4064-byte frame** (of which 72 bytes are SSA value slots). It is
  consistent with expansion of header inline helpers/macros, but
  individual object provenance has not yet been verified.
- `ip6_xmit` (source lines 261–381): IPv6 header preparation (279–346)
  and an `NF_HOOK` invoking `dst_output` (360–366). Runtime Core
  trace reports **592 CoreObjects** and a **2288-byte frame** (72 bytes
  SSA). Together with `ip6_finish_output2` this is a large, nested
  stack demand on the measured TCP child-connect path.
- `ip6_output` (lines 226–245): delegates through
  `NF_HOOK_COND(..., ip6_finish_output)`. Changing the entire
  `ip6_output.o` swaps several functions simultaneously; object-level
  A/B alone **cannot attribute the failure to `ip6_finish_output2`**
  without narrower function-level evidence.

Observed QEMU GDB entry SP values on the failing MiniC Image:
`tcp_v6_connect` 14448 B available →
`__tcp_transmit_skb` 12928 →
`ip6_xmit` 11184 →
`ip6_output` 8608 →
`ip6_finish_output2` **7088 B available upon entry**.
Its 4064-byte frame may leave only about 3024 B before child
calls/interrupt stack demands; this is a *budget calculation*, not
proof that a particular later callee is erroneous. The exception
reported about 96 B remaining, so the exact first crossing still needs
to be sampled.

MiniC implementation inspection:

- Original `src/target/riscv64/core_codegen.c::core_frame_initialize`
  lays out CoreObjects in object order, then SSA values, saved registers
  and call/variadic areas.
- Runtime `apply-riscv64-core-object-slot-reuse-v0.py` pools some
  scalar CoreObjects in 8-byte slots, but excludes objects with
  escaping/derived addresses, aggregate objects, cross-block uses etc.
- `apply-riscv64-core-object-slot-reuse-cfg-hotfix-v0.py` conservatively
  *disallows* sharing slots between reusable objects in different
  basic blocks. The earlier trial removing this restriction cut frame
  size but produced worse out-of-stack SP failures (CI 38227315248).
  It cannot be adopted without a real CFG-aware lifetime proof.

Priority after P1 two-GCC reduction:
(1) reconstruct exact nested call path beyond `ip6_finish_output2`
to the first stack-boundary crossing; (2) emit per-function frame
component totals **including permanent CoreObjects, reusable slots,
call/record return space, padding**, and map large objects to
their originating inline function and address uses; (3) add a
small C regression for whichever lifetime/call-boundary issue
is actually demonstrated. Do not equate smaller stack frames
with compiler correctness.


## Two-GCC pair matrix — completed, CI 38239111412

Each unique linked Image was booted three times under the original Linux
6.6.143 P1 QEMU syscall oracle. The immutable 2064 MiniC object pool and
independent GCC reference were SHA-validated. The 2064/2064 all-MiniC
control continued to FAIL in the same job.

| GCC owners (other 2062 from MiniC) | Exact Image SHA-256 | P1 results |
| --- | --- | --- |
| `net/ipv6/ip6_output.o` + `net/ipv6/tcp_ipv6.o` | `8ac20597070cc51046ef4d50a008297196daebfa36caf86af65a897331e11130` | **PASS / PASS / PASS** |
| `net/ipv6/ip6_output.o` + `net/ipv4/tcp_output.o` | `05fae5803b4f348efdc4e753526c9bdb816da43b75a5c5c841e8d29e81c09b15` | FAIL / FAIL / FAIL |
| `net/ipv6/ip6_output.o` + `net/core/dev.o` | `3528055285cde87b7e26b8b7abbdc0cd98e3c8c2beee243b2517d4a2d9cde030` | PASS / FAIL / PASS |

**Current smallest triple-PASS GCC replacement** contains only
`ip6_output.o` and `tcp_ipv6.o` (2062 MiniC objects). This is a
reproducible mixed-toolchain **workaround**, NOT a proof that precisely
two compiler bugs exist; the second GCC object can merely change
stack-depth/call behavior or binary layout.

Earlier one-GCC `ip6_output.o` replacement gave PASS/PASS/FAIL on
the same pinned image (38224538679), so it is not a certified
reduction. The current three pair tests all include `ip6_output.o`;
therefore the matrix does not establish a globally minimal subset
without additional monotonicity proof or exhaustive combinatorial tests.

On the failing `ip6_output.o`+`tcp_output.o` pair, guest SP
frequently escaped far below the task stack (e.g.
`0xff1fffffffffffe0`), a fact that makes a separate ABI/stack-pointer
corruption hypothesis worth preserving alongside ordinary stack-frame
budget exhaustion. Debugger evidence must distinguish first improper
SP change from legitimate nested stack use before modifying codegen.

Next diagnostic: inspect `ip6_output.o` and `tcp_ipv6.o` per-function
CoreObject types and location in the P1 call chain. Compare failing
all-MiniC vs stable two-GCC pair, then construct a source-level
minimized compiler regression. Reject unproven cross-block stack-slot
sharing.


## Controlled GDB call-entry evidence — CI 38241421894

The CI run **SUCCEEDED** as a diagnostic job. It generated the exact
all-MiniC P1 FAIL, then three P1 boots of each two-GCC replacement,
and additionally ran observational GDB on two already-linked Images.
The GDB sessions are **not** certifications of P1 behavior.

All-MiniC GDB entry SP: `tcp_v6_connect = 0xff200000001cb870`.
The 16 KiB task stack low address was
`0xff200000001c8000`. With `ip6_output.o` and
`tcp_ipv6.o` replaced by GCC, entry SP in `tcp_v6_connect`
was **identical**. Later common-function entry SPs differed:

| Function entry (same P1 TCP path) | All MiniC remaining stack | 2 GCC remaining stack | GCC-pair advantage |
| --- | ---: | ---: | ---: |
| `tcp_v6_connect` | 14,448 B | 14,448 B | 0 |
| `__tcp_transmit_skb` | 12,928 B | 13,568 B | 640 B |
| `ip6_xmit` | 11,184 B | 11,824 B | 640 B |
| `ip6_output` | 8,608 B | 11,520 B | 2,912 B |
| `ip6_finish_output2` | 7,088 B | 11,312 B | 4,224 B |
| `__neigh_create` | 3,024 B | 11,152 B | **8,128 B** |
| `neigh_resolve_output` | 2,896 B | 11,152 B | 8,256 B |
| `__dev_queue_xmit` | 2,592 B | 10,848 B | **8,256 B** |

This directly shows that the MiniC-generated frames in the
`tcp_v6_connect → ip6_xmit → ip6_output → ip6_finish_output2`
chain cumulatively consume roughly **8 KiB more task stack**
*before* reaching downstream neighbor/device routines.

In the all-MiniC GDB run, the second call to
`__list_add_valid_or_report` began with **only 224 bytes**
remaining and the kernel subsequently panicked with stack SP
`0xff200000001c7f70` (144 B below task-stack base).

**Important confounder:** The independently tested 2-GCC Image
PASSED the normal P1 test 3/3 on run 38241421894,
but **the same 2-GCC Image panicked when run under repeated GDB
breakpoints**, later in `decay_load`, with anomalous SP
`0xff1fffffffffffd0`. Thus this GDB run must NOT be
called a successful debug-guest control. Breakpoints change
timing/interleaving, and/or expose a still-latent bug.
The pre-crash frame-entry comparison is useful evidence about stack
footprint but cannot by itself prove that the 8 KiB difference is the
*only* runtime correctness issue. No compiler core fix is validated yet.

Repeat two-GCC results under ordinary no-debug QEMU in this run:

- `ip6_output.o + tcp_ipv6.o`: **PASS/PASS/PASS** (new linked Image
  SHA `15de4cfd2a821b652b6c9d11a2586ab581cf19fcddedb123a86cef99707ed142`).
- `ip6_output.o + tcp_output.o`: PASS/FAIL/FAIL.
- `ip6_output.o + dev.o`: PASS/PASS/PASS this time,
  versus PASS/FAIL/PASS in CI 38239111412. **Do not interpret a
  single 3/3 as a statistical guarantee of long-term stability.**

Implementation detail: the initial GDB script emitted a literal
`\\n` in its first record and accidentally interpreted an
emergency overflow-stack SP as a task-stack offset; its summary
was incomplete. Follow-up fixes `faf37daf` and `ad79d93f`
correct future logging, but the raw `gdb.log` and QEMU serial
from CI 38241421894 remain authoritative.

Next: use an exact function-local codegen/IR frame-component audit
with fixed MiniC compiler and identical .i preprocessing input;
isolate the growth of CoreObjects from source/header inline
expansion. Test the resulting safe lowering improvement with one
TU recompile and same P1 oracle, not with a wholesale unsound
cross-block slot reuse.


## Byte-identical MiniC frame accounting — CI 38245948250 (PASS)

The original frame-capture attempt (#38223005780) failed with zero_frame_traces
because the compiler did not implement the exported diagnostic environment variable.
A new post-profile **stderr-only** compiler instrument has now PASSed the SHA
equality test: all three replayed objects (ip6_output.o, tcp_ipv6.o,
tcp_output.o) are **byte-identical** to their original certified MiniC objects.
The diagnostic compiler emitted no modified C object bytes or assembly;
no Linux Image was linked or booted in this capture-only test.

| Function | frame B | CoreObject region B | SSA spills B | other B | objects | unique starts | shared starts |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ip6_finish_output2 | **4064** | **3976** | 72 | 16 | 1101 | 497 | 604 |
| ip6_xmit | **2288** | **2208** | 72 | 8 | 592 | 279 | 313 |
| tcp_v6_connect | 864 | 808 | 40 | 16 | 229 | 88 | 141 |
| __tcp_transmit_skb | 1440 | 1376 | 56 | 8 | 373 | 181 | 192 |

Every one of ip6_finish_output2's 1101 CoreObjects is scalar:
1079 are 8 bytes, 19 are 4 bytes, and 3 are 1 byte. Its 497
distinct physical stack-slot start offsets exactly fill 3976 bytes
(in 8-byte increments); 604 objects already reuse other starts.
For ip6_xmit, all 592 objects are scalar: 503 x 8 B, 74 x 4 B,
11 x 1 B, 4 x 2 B. Large C aggregate storage and SSA spill
allocation have been ruled out as primary causes of these frames.

The remaining investigation is WHY so many scalar CoreObject slots
are reserved in those few kernel functions. Cross-basic-block slot
sharing must prove full value lifetime; the earlier unconditional
cross-block trial #38227315248 was unsafe. Merely proving that all
accesses are in one block does not prove the value is dead at exit.

Provenance: CI #38245948250; diagnostic artifact files:
p1-frame-components/components.tsv, objects.tsv, summary.tsv;
per-function frame accounting and parser self-test PASS.
No compiler runtime fix has yet been certified.

## Function-scoped single-owner runtime experiment — CI #38252791263

A single opt-in experiment changed **only** the CoreObject cross-block allocation for `ip6_finish_output2` within `ip6_output.o`, with `ip6_xmit` explicitly retaining the prior **2288-byte** frame. The selected function dropped **4064 → 2224 bytes**; 2062 other MiniC objects were frozen, and only `tcp_ipv6.o` came from GCC. The exact same linked Image SHA `25fb4a49e59f3a72fab549ee7ada7ca44e94080c9413d7cc4de0afeff274d64f` failed normal P1 **FAIL / FAIL / FAIL**, with overflow fingerprints in `timekeeping_get_delta` and `arch_local_irq_save` and abnormally distant SP near `0xff1fffffffffffe0`.

Thus **even a single function's 1840-byte frame reduction is not a certified fix**; do not promote the experimental cross-block allocator into the default compiler. Earlier whole-`ip6_output.o` experiment changed both `ip6_finish_output2` and `ip6_xmit` and produced 1/3 PASS. This makes additional stack contributors or a separate code-generation defect plausible, but not proven.

The next priority is to attribute the **259 permanent/nonreusable scalar CoreObjects** occupying ~2 KiB of the 4064-byte `ip6_finish_output2` frame, and to compare a pinned no-experiment GCC-`tcp_ipv6.o` control with experimental frames. Only then consider a proof-backed improvement to the lowering rules.

## Why 259 scalar stack objects cannot be reused — CI #38253905548

The observational, SHA-verified compiler audit recompiled exactly `ip6_output.o`, `tcp_ipv6.o`, and `tcp_output.o`; **all three emitted ELF objects were byte-identical to the frozen originals**. It classified every one of 2340 relevant CoreObjects, with complete parser coverage. This CI did **not** link or run a Linux image.

| Function | CoreObjects | Already reusable | Permanently allocated | Address derived / other uses | Address invalid or accessed in multiple blocks |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ip6_finish_output2` | 1101 | 842 | **259** | **224** | 35 |
| `ip6_xmit` | 592 | 463 | **129** | **96** | 33 |
| `__tcp_transmit_skb` | 373 | 311 | 62 | 17 | 44 (plus 1 shape) |
| `tcp_v6_connect` | 229 | 208 | 21 | 0 | 16 (plus 5 shape) |

**Key insight:** The 4064-byte `ip6_finish_output2` frame is dominated by scalar CoreObjects rather than SSA spills or large C aggregates. Of its 259 nonreusable scalar objects, **224 are denied by the strict rule that an `OBJECT_ADDRESS` SSA result may be consumed only by direct LOAD/STORE**. This category conflates harmless address derivation (`POINTER_OFFSET`, `FIELD_ADDRESS`) with genuine address escapes or unproven uses. It would be unsound to free these objects without distinguishing the cases. We therefore added a read-only consumer-type audit that can identify which instruction kinds consume these 224 address values before changing any lowering or allocator rule.

Artifact provenance: CI #38253905548; all 3 selected `.o` SHA-identical; `nonreuse-reasons.tsv` reports all six audited functions. The follow-up derived-use breakdown is an observational audit only; no P1 fix is claimed.


## Derived-address consumers resolved — CI 38254659162

Important correction to our prior hypothesis: the 224 disqualified objects in
`ip6_finish_output2` are **NOT predominantly pointer arithmetic**. The
SHA-identical Core diagnostic observed their actual use instruction kinds.

| Function | Objects disqualified by non-direct address uses | Consumer instructions |
| --- | ---: | --- |
| `ip6_finish_output2` | **224** | **112 stored-pointer `STORE` instructions (kind 35), 112 `SCALAR_EQUAL` instructions (kind 14)** |
| `ip6_xmit` | **96** | 48 stored-pointer `STORE`, 48 `SCALAR_EQUAL` |
| `__tcp_transmit_skb` | **17** | 10 stored-pointer `STORE`, 8 `SCALAR_EQUAL` |

No direct `POINTER_OFFSET` or `FIELD_ADDRESS` consumption was observed in
this audited subset. In these cases address identity and escape semantics are
real, so blind stack-slot reuse can change the result of pointer comparison
or leave a dangling pointer. The previous pointer-arithmetic theory was wrong
for this subset. **Do not relax the existing 224-object restriction.**

Next: identify the source spans and lowering sites generating the 112 paired
pointer comparisons / stored pointer values. Prove whether these are redundant
compiler-introduced temporaries before touching codegen. Full-MiniC P1 is
still unverified/FAIL, and this remains a diagnostics-only result.


## Independent P1 timer/IRQ-stack hypothesis — 2026-10-13

**Original frozen 16KiB Linux config**, 2064 certified MiniC C objects, GNU
assembler/linker, no MiniC compiler source modification or new full C build:

- All-2064-MiniC baseline QEMU P1: **FAIL**, exact Image SHA
  `17ef75c9e2bd51cb5e2d277a1897c1a3f442307d337e80c2eb9fbf90c2e73785`.
- Replacing only `net/ipv6/ip6_output.o` and `kernel/sched/fair.o` with
  authenticated GNU C objects, leaving the other **2062** MiniC objects
  intact: **PASS/PASS/PASS**, identical linked Image SHA
  `a68d5ab01adbe7720c0a4df41fd3c645e69fafca5adfbde0051489448e5e2c8f`.
- CI run **#38326099444**, artifact `11780099513`, original
  `p1-rescue-pairs.tsv` and `p1-rescue-pairs-repeats.tsv` retained.

This partner was not chosen at random: previous GDB IRQ-stack snapshot
#38270171284 includes scheduler/PELT return-address *candidates* such as
`__accumulate_pelt_segments`, `___update_load_sum` and
`update_cfs_rq_load_avg`, but the snapshot is **not a certified unwind**.
In the separate single-TU 16KiB discarded-equality experiment
#38324862048, IPv6 `ip6_finish_output2`/ `ip6_xmit` frames shrank from
4064/2288 to 2272/1520 bytes; the **2064-MiniC** candidate still failed
P1 0/3 and its trapped SPs were near/below the known CPU0 16KiB IRQ-stack
lower boundary.

**Interpretation limits:** this establishes another reproducible 2-GNU-object
P1 rescue in the normal 16KiB configuration. It does NOT establish a MiniC
compiler fix, prove `fair.o` alone corrects P1, prove the two failures are
independent, or certify 16KiB full-MiniC P1. Mixed-object changes can affect
stack consumption, symbol placement, timing, and interrupts. No stack-slot
lifetime restriction has been relaxed. Next focus: obtain *read-only*
per-function frame accounting for `kernel/sched/fair.o` (timer/PELT path)
while preserving the already proven IPv6 task-stack evidence, then make
strictly localized, verified changes only if a valid lowering defect is shown.


## Frozen `fair.o` frame attribution + singleton control — 2026-10-13

**Observation only** in existing canonical GitHub Actions receiver; the
original Runtime MiniC binary was instrumented for stderr traces and the
single GNU-preprocessed `kernel/sched/fair.o` was replayed. Output ELF was
**byte-identical to the pinned MiniC object**, with no Image link or boot.
CI **#38327693568**, evidence artifact **11782230460**.

| fair.o function | MiniC frame bytes | CoreObject region | SSA spill region |
| --- | ---: | ---: | ---: |
| `load_balance` | 800 | 744 | 40 |
| `update_cfs_rq_load_avg` | 544 | 496 | 32 |
| `enqueue_entity` | 272 | 224 | 32 |
| `update_load_avg` | 224 | 176 | 32 |
| `task_tick_fair` | 176 | 128 | 32 |

The unfiltered compiler `CORE_FRAME_REUSE` summary covered 600 emitted
functions. None of its reported `fair.o` frames exceeded 800 bytes.
The scheduler has nontrivial stack demand but no individually observed
4-KiB frame comparable to IPv6 `ip6_finish_output2`. This does **not**
bound the combined nested call-chain depth, prove correct frame lifetimes,
or establish that IRQ is the original failing stack.

**Single-GCC-owner causal control**: CI **#38328031823**, artifact
**11781527855**, froze 2064 MiniC C objects and tried only GCC
`kernel/sched/fair.o` (2063 MiniC + 1 GCC), retaining the unchanged
16-KiB Linux config. Exact baseline all-MiniC Image SHA
`0ffaf1f87b5935ddd26c0c5f9b85f192d545802dd01da70fe8e04aa9f778ef89`
was FAIL/FAIL/FAIL. Exact one-GCC-fair Image SHA
`6d1d364e422469d12c816799f8bc8c9eab316e8535d901182dc801768622a0d5`
also **FAIL/FAIL/FAIL**, all at the IPv6 child-connect syscall. The
trapped SP was exactly **144 bytes below the task stack base**
(`__list_add_valid_or_report+0x1c`) in the saved ordinary-QEMU logs.
That CI is RED due to its requested causal candidate not rescuing P1;
the evidence, provenance and logs were collected successfully.

Previously the double-GCC `ip6_output.o` + `fair.o` Image passed 3/3
(#38326099444); this new singleton shows `fair.o` **alone is not
sufficient**. This pair result must not be treated as proof that the
scheduler is a distinct compiler defect: object substitutions can affect
calling depth, stack budget, binary layout and interrupt timing. The
independent frozen all-MiniC GDB first-fault capture
(#38270171284) ALSO classified the faulting SP as **task stack**, not
IRQ; observations on IRQ stack during timer interrupts are not an IRQ
overflow certificate. Some earlier *different experimental images* did
have separate IRQ-boundary evidence, and must not be conflated.

Next 16-KiB repair focus: direct IPv6/task-stack codegen frame
budget and known nonreusable CoreObject/lifetime evidence. No global
cross-block stack slot sharing and no additional CI workflow.


## Actual PELT owner correction and test — 2026-10-13

**Correcting earlier attribution:** Linux 6.6.143 `kernel/sched/build_policy.c`
includes `pelt.c` when `CONFIG_SMP` is enabled. Thus the emitted
`decay_load`, `accumulate_sum`, `___update_load_sum` and
`__accumulate_pelt_segments` implementations belong to
**`kernel/sched/build_policy.o`**, not `kernel/sched/fair.o`.
The fair.o stack-frame audit remains valid, but it did not audit these
specific PELT functions. Earlier two-GCC fair.o rescues cannot be
attributed to a PELT implementation replacement.

One controlled P1 test using default 16KiB stacks, the same frozen
2064-object universe, and GCC replacements **only** for
`net/ipv6/ip6_output.o` and `kernel/sched/build_policy.o` gave
**FAIL/PASS/FAIL (1/3)** on ONE SHA-identical Image,
`84cb967a80c0d24d0f77d771c28402ff0770e2a8107afd0a190fab9ac951afdc`.
CI #38329563821; evidence artifact #11782515834,
`p1-rescue-pairs-repeats.tsv`. The all-MiniC P1 baseline failed.
The two failed ordinary QEMU boots had IRQ-stack-base SP
`0xff1fffffffffffe0` at `debug_spin_lock_before` and
`_raw_spin_lock_irqsave`, respectively. This candidate is **not
certified** and does NOT prove a PELT compiler bug.

Our original unmodified 16KiB all-MiniC first fault was a **task**
stack overflow; other *different linked Images* reached an **IRQ**
stack boundary. They cannot be conflated into one proven defect.
The 32KiB full-MiniC 3/3 result is a capacity control, not a 16KiB
compiler repair. Next work should identify a specific repeatable
frame/ABI lowering defect from pinned GDB and CoreObject evidence
before proposing codegen changes; no more speculative owner swapping.


## Byte-identical actual PELT frame audit — 2026-10-13

CI #38330451373 (artifact #11783035565) recompiled ONLY
`kernel/sched/build_policy.o` with stderr-only Core frame instrumentation.
The output `.o` was **byte-identical to its authenticated frozen MiniC
counterpart**; no kernel Image was linked, no QEMU P1 was run.

| PELT function in build_policy.o | MiniC frame bytes | CoreObject region | SSA bytes |
| --- | ---: | ---: | ---: |
| `decay_load` | 144 | 96 | 32 |
| `__accumulate_pelt_segments` | 80 | 40 | 24 |
| `accumulate_sum` | 224 | 184 | 32 |
| `___update_load_sum` | 208 | 152 | 48 |
| `___update_load_avg` | 64 | 32 | 24 |
| `update_rt_rq_load_avg` | 128 | 64 | 48 |

These are modest **individual** frames, though their *nested* sum and
other callee frames could be significant. A GDB trace that ends at
`decay_load` does not mean it is the source of overflow: it may simply be
the next caller/stack allocation after earlier frames consumed the budget.
The failed `ip6_output.o + build_policy.o` GCC pair (1/3 P1) reinforces
that this is not a proven one-owner compiler bug. The next investigation
should measure the actual nested IRQ path and a pinned first-fault SP,
not swap more large objects by intuition.


## IPv6-only semantic dead-address removal, original 16KiB stack — 2026-10-13

One *read-only-with-respect-to-production-code* trial applied the existing
experimental `apply-p1-pure-discarded-local-address-equality-v1.py` only
when compiling `net/ipv6/ip6_output.o`, and allowed its existing strict
side-effect-free guard in **all functions of that TU**, rather than only
`ip6_finish_output2` or `ip6_xmit`. All remaining 2063 authenticated
MiniC C objects stayed byte-identical; **zero GCC C-object substitutions**.
The original 16KiB task/IRQ configuration was unchanged. CI
**#38331510555**, artifact **11783586392**.

| Linked call-chain function | Original MiniC frame | One-TU candidate frame |
| --- | ---: | ---: |
| `ip6_xmit` | 2288 B | 1520 B |
| `ip6_output` | 768 B | 512 B |
| `ip6_finish_output` | 224 B | 224 B |
| `__ip6_finish_output` | 288 B | 288 B |
| `ip6_finish_output2` | 4064 B | 2272 B |
| **Sum of those frames** | **7632 B** | **4816 B** |

The 2816-byte frame saving was verified by the actual replayed MiniC
`ip6_output.o` and the frozen-object overlay manifest. Three independent
P1 QEMU boots of the **same** Image SHA
`82bb5bf30d5226db5ab18d4597e796aeb0606d633cc3d65be6c9e25086442067`
returned **FAIL/FAIL/FAIL** in IPv6 child-connect. The three serial
panic logs give trapped SPs of `0xff1fffffffffffb0` (80 bytes below
`0xff20000000000000`) and `0xff1fffffffffffe0` (32 bytes below,
twice), with varying interrupt/timer-related PCs:
`__minic_inline_spec_1331_185` called from `decay_load`,
`get_current`, and `__radix_tree_lookup`. The CPU0 IRQ stack base
`0xff20000000000000` was independently *measured* in a prior
GDB run on another linked 16KiB Image; the new trial did not itself run
GDB, so classification of its trap as IRQ-stack overflow remains a
strong inference, **not fresh same-Image proof**.

For the **unchanged original** MiniC Image, the pinned GDB
`P1_FIRSTFAULT` instead showed task-stack SP 144 bytes *below* base.
The same original entry trace measured remaining stack:
`__tcp_transmit_skb` 12928, `ip6_xmit` 11184,
`ip6_output` 8608, `ip6_finish_output2` 7088,
`__neigh_create` 3024, `__slab_alloc_node` 752,
`get_freepointer_safe` 480, `__list_add_valid_or_report` 224 bytes.
The 4064-byte entry-SP difference between
`ip6_finish_output2` and `__neigh_create` matches the
measured 4064-byte MiniC frame exactly.

**Decision:** reducing only the IPv6 C owner's stack frames, even by
2816 bytes along the observed path, does **not** certify 16KiB P1.
Do not promote this experimental lowering into production solely as a
P1 workaround. Analyze stack budgets separately for the initial
task-stack failure and the post-optimization interrupt-path failure,
using same-Image first-fault evidence before changing backend ABI or
unsafe CFG-wide stack-slot reuse. All temporary CI script changes were
reverted; no additional Workflow was introduced.


## Verified same-Image IRQ first fault after safe IPv6 frame shrink — 2026-10-13

CI **#38333385948** (artifact **11783932483**) reused the original
16KiB Linux config and the immutable 2064-object MiniC universe, but
recompiled ONLY `net/ipv6/ip6_output.o` using the existing strict
side-effect-free discarded plain-local address-comparison lowering.
The other **2063 objects remained byte-identical MiniC objects**;
no GCC C object was substituted. The first three **ordinary, no-GDB**
QEMU boots of the SAME linked Image
`b13983e96786df599c0686476a2409effa118bd68ec1382849d05e930440753f`
failed P1 **0/3**. Only after these ordinary boots did the existing
read-only GDB diagnostic run on that exact Image and linked `vmlinux`.

**GDB directly measured CPU0's 16KiB IRQ stack**, not an assumed or
task-stack-relative range:
`irq_base=0xff20000000000000`,
`irq_top=0xff20000000004000`.
The separately captured serial-register SPs from the three normal boots
were respectively **16, 64, 32 bytes below IRQ base**, and the
same-Image attribution check reported
`P1_DEFINED_CROSSBLOCK_IRQ=VERIFIED`. This supersedes the earlier
"strong inference only" for the previous **different Image**.

The read-only GDB run captured a genuine first exception:
`P1_FIRSTFAULT epc=0xffffffff8569768c`
(`riscv_clocksource_rdtime+0x2`),
`ra=0xffffffff80711d9c` (`tk_clock_read+0xaa`),
`sp=0xff1fffffffffffe0` (**32 bytes below IRQ base**),
`badaddr=0xff1ffffffffffff8` (SP+24),
`cause=15` (store page fault).
This is consistent with a small clock-read function's initial
stack push touching the guard page, **not** evidence that clocksource
itself allocated an abnormally huge frame.

Crucially, the same debugger sampled normal earlier timer IRQ entries
at `riscv_timer_interrupt SP=irq_base+15744`, then a later entry
at `irq_base+1040`; `hrtimer_interrupt` likewise appeared at
`irq_base+15680` and later `irq_base+976`. Thus **14704 bytes of
IRQ-stack headroom disappeared BEFORE the later timer handler was
entered**. This cannot be explained by a 32-byte `rdtime` callee frame.
The exact cause (IRQ nesting, a deep callback already executing on IRQ
stack, or a faulty stack-pointer/interrupt-state restoration) remains
**unproven**; the saved IRQ-stack binary contains candidate return
addresses but those are NOT authenticated unwinds.

**Decision:** stop attributing this experimental IRQ fault to
`decay_load`, `get_current`, `__radix_tree_lookup`, or a single
IPv6 frame without evidence. The decisive next small investigation
should compare **IRQ entry/exit stack restoration and nesting**, or
obtain a validated call-depth/backtrace from the pinned IRQ stack.
Existing canonical script was restored byte-for-byte after this
diagnostic, and no new Workflow, branch, or production MiniC patch
was retained. No claim of a 16KiB fully-MiniC P1 pass is made.


### Source-side interpretation of the verified IRQ fault

Reviewed Linux 6.6.143 `drivers/clocksource/timer-riscv.c`:
`riscv_clocksource_rdtime()` simply returns `get_cycles64()`.
Its first-fault EPC at function+2, SP=IRQ-base-32, and store-fault
address=SP+24 are consistent with a small entry prologue being the
*last* stack push, not a giant `rdtime()` frame.

Reviewed `arch/riscv/kernel/traps.c::do_irq()` (CONFIG_IRQ_STACKS).
The source switches to the **top of IRQ stack only if**
`on_thread_stack()` is true; otherwise it calls `handle_riscv_irq()`
on the currently active stack. The later `riscv_timer_interrupt()`
entry with merely 1040 bytes headroom might therefore reflect
re-entry while already on the IRQ stack, a deep prior IRQ-context
call path, or a bad stack/IRQ-state restoration. It is not yet
evidence for any one of those mechanisms. The next diagnostic must
measure the branch decision and stack pointer **at IRQ entry**, and
track returns, before altering MiniC's inline-assembly handling or
loosening CoreObject lifetime analysis.


## Frozen receive-path Core frame replay — 2026-10-13

CI **#38335183624**, artifact **11784965453**. The pre-existing
frame-capture lane recompiled exactly four owners with the original
diagnostic-only MiniC profile: `net/ipv6/tcp_ipv6.o`,
`net/ipv4/tcp_input.o`, `mm/slub.o`, `mm/page_alloc.o`.
Every replayed object was **byte-identical** to its frozen counterpart.
**No Linux Image was linked, no QEMU runtime test was performed.**

Frame sizes from the replay's `CORE_FRAME_REUSE` compiler records:

| Function | Frozen MiniC RV64 frame |
| --- | ---: |
| `tcp_v6_rcv` | 1760 B |
| `tcp_v6_do_rcv` | 1104 B |
| `tcp_rcv_state_process` | 1776 B |
| `tcp_v6_conn_request` | 352 B |
| `tcp_conn_request` | 1312 B |
| `__slab_alloc` | 128 B |
| `___slab_alloc` | 976 B |
| `new_slab` | 160 B |
| `__alloc_pages` | 352 B |
| `__alloc_pages_slowpath` | 1024 B |

Detailed `CORE_FRAME_COMPONENT` records attribute **1232/1312 B**
of `tcp_conn_request`, **304/352 B** of `tcp_v6_conn_request`,
**112/160 B** of `new_slab`, and **272/352 B** of
`__alloc_pages` to the CoreObject region. On these verified functions
the non-SSA object area, not SSA spill bytes, dominates.

**Audit tooling limitation:** the overall job is **RED**, *not*
because MiniC compilation or SHA comparison failed, but because the
legacy report parser keys functions by *unqualified symbol name*
across four TUs. Identically named header helpers, notably
`alloc_pages`, yield `ValueError: duplicate header: alloc_pages`.
The explicit per-function trace lines above are nevertheless saved
in `p1-frame-components/replay.log` and the four SHA checks passed.
The aggregate parsed report did NOT pass and is not claimed certified.

The same-Image IRQ stack dump from #38333385948 contains aligned
code-address *candidates* near `tcp_v6_rcv`, `tcp_v6_do_rcv`,
`tcp_rcv_state_process`, `tcp_v6_conn_request`,
`tcp_conn_request`, `__slab_alloc`, `new_slab` and
`__alloc_pages` in a plausible top-to-bottom order. These are not
authenticated unwinds; their simultaneous activity and precise
stack-frame summation have NOT been proven. This narrows priority to
the receive-to-slab stack budget. Do not infer a broken ABI or
promote speculative cross-block slot reuse from this audit alone.

**Repository hygiene:** original diagnostic script is restored exactly,
no new branch/Workflow, no production MiniC changes, no extra runtime
build. Next targeted work: establish an **actual** sampled nested
call/stack-depth frontier or compare GNU/MiniC frame budgets for a
specific expensive receive-path function before proposing a fix.


## Actual IRQ-stack TCP receive call frontier — 2026-10-13

CI **#38336624015**, artifact **11785257966**. One GDB run on a
**SHA-pinned** Linux Image
`a0e13466e179ddb5f9c4201159068c5a0811529311c807ceed594a63d3d45bd4`
after three ordinary-QEMU P1 failures (0/3); original 16KiB stacks,
2064 MiniC objects (only `ip6_output.o` rebuilt under the pre-existing
effect-free address-comparison experiment), zero GCC-C substitutions.
GDB breakpoint **entry SP samples** on this exact Image, unlike the
prior raw-stack word candidates:

| Actual function entry | IRQ-stack bytes remaining |
| --- | ---: |
| `handle_softirqs` | 15968 |
| `net_rx_action` | 15712 |
| `process_backlog` | 14944 |
| `ipv6_rcv` | 14528 |
| `tcp_v6_rcv` | 12256 |
| `tcp_v6_conn_request` | 7616 |
| `tcp_conn_request` | 7264 |
| `___slab_alloc` | 4720 |
| `new_slab` | 3744 |
| `__alloc_pages` | 2816 |

These entries make a **real receive/SoftIRQ → TCP request → SLUB →
page-allocation stack-pressure frontier** rather than merely matching
saved addresses from a postmortem dump. Uninstrumented intervening
functions still exist; the table is **not** a fully verified
direct caller-to-callee chain.

On the same GDB run first fault was at
`get_current+0x10`, return address `preempt_count_ptr+0x14`,
SP `0xff1fffffffffffe0`, 32 bytes **below measured CPU0 IRQ base**;
three ordinary boots also trapped **32/32/32 bytes below** the verified
IRQ base. Therefore a nested timer interrupt is **not necessary** to
explain this reproducible *receive-path* IRQ stack exhaustion
(other experiments trapped at timekeeping). This is not evidence of a
`get_current` miscompile: the prologue encountered an exhausted stack.

### Existing within-basic-block allocator capacity audit

From #38335183624's immutable `CORE_FRAME_OBJECT` /
`CORE_FRAME_INTERVAL` traces, recomputed object-slot coloring offline
using each block's maximum simultaneous inclusive live intervals.
Because the certified backend never allows cross-block sharing, the
**sum of these per-block overlap maxima is a lower bound** for the
current policy; current allocations match it exactly:

| Function | Reusable scalar objects | Basic blocks with reusable slots | Actual 8-byte slots | Within-block proven minimum |
| --- | ---: | ---: | ---: | ---: |
| `tcp_conn_request` | 213 | 42 | 94 | 94 |
| `tcp_v6_conn_request` | 90 | 17 | 33 | 33 |
| `new_slab` | 27 | 5 | 11 | 11 |
| `__alloc_pages` | 48 | 8 | 22 | 22 |
| `__alloc_pages_slowpath` | 148 | 52 | 107 | 107 |

In particular `tcp_conn_request` already reuses **213/260**
CoreObjects. Its 94-slot pool uses **752 bytes** of a
**1232-byte** CoreObject region; 47 permanent objects and layout
account for the remainder. Among its reusable objects, all 213
observed first accesses are **WRITE**. Merely reordering the
existing within-block greedy allocator cannot improve any of these
observed slot counts. Further frame reduction requires (a) *proven*
CFG-cross-block live-range safety or (b) elimination/shortening of
compiler-generated temporaries. Earlier cross-block *P1 failure* alone
does not prove a lifetime corruption; equally it must not be promoted
as a runtime fix without a correctness proof and regression.

Next inexpensive causal comparison: freeze one
`net/ipv4/tcp_input.o` only and compare GNU versus MiniC function
prologues while capturing a single-TU Core frame report. This avoids
the old report parser's duplicate helper symbols across four TUs;
no Linux link or new CI workflow.


## Frozen GCC versus MiniC RV64 stack prologues — 2026-10-13

Existing canonical CI **#38337439838**, artifact **11785571487**.
Recompiled just `net/ipv4/tcp_input.o` with **stderr-only**
frame instrumentation and verified emitted .o SHA identical to the
frozen MiniC counterpart. No Image link. GNU and MiniC prologues
were extracted from their original authenticated `.o` files:

| Function | GCC immediate SP decrement | MiniC immediate SP decrement | Excess |
| --- | ---: | ---: | ---: |
| `tcp_conn_request` | 304 B | 1312 B | 1008 B |
| `tcp_rcv_state_process` | 128 B | 1776 B | 1648 B |

Combined **potential** static-frame excess = **2656 bytes** if both
frames are simultaneously active on the tested chain; a pair of
prologue sizes is NOT itself proof they were concurrently live.
MiniC's CoreObject regions measure 1232 and 1712 bytes; SSA
value regions 72 and 48 bytes, respectively. This is direct evidence
of large MiniC stack-layout overhead rather than an identified
incorrect SP instruction. The prior GDB RX frontier supports
prioritizing TCP functions over isolated clocksource/scheduler frames.

Next controlled check: apply the existing strict, effect-free
*discarded local address-comparison* lowering to **only**
`net/ipv4/tcp_input.o`, recompile once, and measure those two
stack frames with the original pinned MiniC object universe.
No Linux Image linking until it demonstrably reduces the measured
frames and focused correctness tests are identified. No new
Workflow or branch, and no production compiler patch.


## Targeted single-TU TCP lowering controls — 2026-10-13

Both experiments used the existing frozen 2064-object MiniC cohort:
only `net/ipv4/tcp_input.o` recompiled; the other 2063 originals
were SHA checked. They did **not** link a Linux Image or execute P1.
No production compiler change was made.

- **Pure discarded local-address comparison elimination** (existing,
  narrowly guarded IR lowering): CI **#38337904153**, artifact
  **11785736005**, SUCCESS. `tcp_conn_request` frame
  **1312 → 1184 bytes** (-128); `tcp_rcv_state_process`
  **1776 → 1392 bytes** (-384), sum **512 bytes** saved
  if both are resident at once. Object provenance check reported
  exactly one changed C owner, 2063 original owners untouched.
- **Existing first-write cross-block stack-slot reuse**, restricted
  to `tcp_rcv_state_process` ONLY: CI **#38338521520**,
  artifact **11786386321**, SUCCESS *for the isolated diagnostic*.
  `tcp_rcv_state_process` frame **1776 → 608 bytes**
  (-1168), while `tcp_conn_request` remained **1312 bytes**.
  Exactly one C owner was changed. The candidate has NOT passed
  focused semantic or Linux P1 runtime verification. An earlier
  more general cross-block reuse variant failed P1, so this result
  **must not be promoted as a safe fix**.

The strongest current evidence is that GNU GCC generates
`tcp_rcv_state_process` with a **128-byte** entry frame while
the certified MiniC baseline needs **1776 bytes**. A large
opportunity exists in stack slots and dead CoreObjects, but only
a conservative, correctness-proven lifetime/interference change
could be adopted. The cross-block heuristic measures an upper
bound on potential frame reductions; it is not proof that
objects from different blocks cannot be live concurrently
through loops, joins, pointer uses, or inlined control flow.

The opt-in diagnostic script was restored BYTE-FOR-BYTE to its
original Git blob after collecting the result. No branch
proliferation, new workflow, or permanent test harness change.


## Straight-line integer RHS / no-spill proof of concept — 2026-10-13

The existing opt-in experimental compiler patch was extended TEMPORARILY
to avoid `spill_scalar_value()` plus `reload_scalar_value()` in
`lower_integer_binary_operands()` **only** when the RHS is an integer
literal or a plain local lvalue, inside the two named TCP functions.
Evaluation order remains left-then-right and an explicit guard rejects
any unexpected Core block change. Unrecognized/CFG-creating RHS keeps
the prior spill path. The older discarded-equality optimization was
explicitly disabled for this experiment.

CI **#38340664131**, artifact **11786168992**, exact frozen 16KiB Linux
pool of 2064 C objects. One TCP owner was initially recompiled, with
2063 original MiniC owners SHA-attested and unchanged in the isolated
overlay; later link/header reconciliation replayed its own required
dependencies with the same MiniC toolchain.

| Function | Original MiniC frame | Candidate frame | Reduction |
| --- | ---: | ---: | ---: |
| `tcp_conn_request` | 1312 B | 1312 B | 0 B |
| `tcp_rcv_state_process` | 1776 B | 1728 B | 48 B |

All **three** regular non-GDB QEMU P1 boots of the same candidate Image
SHA `bdb3dac8aac6c259262d5b7cec86d78bba07d4dc84dcd41e1bfb5f22623d883c`
**failed** (0/3); the pre-experiment all-MiniC 16KiB control also
failed, as expected. The overall GitHub workflow was green because
its *diagnostic harness* correctly completed, **not** because P1
passed. This is an isolated measurement only, not a correctness or
runtime certification of the new lowering.

**Decision:** this extremely narrow transformation did not meaningfully
reduce the multi-kilobyte receiving stack frontier and did not rescue
16KiB P1. Do not merge/promote it; return the opt-in scripts to their
original exact blobs. For any subsequent generalization, first prove
a CFG-free RHS (not just integer typed), semantic C evaluation-order
and observable volatile/asm behavior, and validate on focused compiler
tests before a full P1 link. No new branch/workflow/production compiler
changes were needed.

## ROI triage before further Core stack optimization — 2026-10-13

**Do not repeat full P1 solely to evaluate a 16/48-byte function frame
saving.** Evaluate the actual `net/ipv4/tcp_input.o` and IRQ/IPv6
call-path stack frames using cached stderr-only Core frame reports first.
Any "stack saving" is an actual measured per-function frame reduction,
not the number of IR objects deleted or apparent source-expression frequency.

| Mechanism, already measured | Affected function(s) | Per-function stack reduction | Confidence |
| --- | --- | ---: | --- |
| Integer RHS literal/plain-local LHS no-spill | `tcp_rcv_state_process` / `tcp_conn_request` | **48 / 0 B** | single owner replay; P1 0/3 (#38340664131) |
| Remove side-effect-free discarded address comparisons | same two TCP functions | **384 / 128 B** | single-owner frame replay, no P1 certification (#38337904153) |
| First-write experimental cross-block slot sharing | `tcp_rcv_state_process` | **1168 B** (1776→608) | frame only; **NOT correctness proven** (#38338521520) |
| Same discarded-address elimination, IPv6 TU | `ip6_finish_output2`, `ip6_xmit`, `ip6_output` | **1792 + 768 + 256 B** | image unchanged across repeated P1 0/3 (#38331510555) |

**Object allocation decomposition** from the immutable
`tcp-original-prologue-frames.zip` / CI #38337439838
(`objects.tsv` joined to `intervals.tsv` by function and ID):

| Function | Objects | Reusable / fixed | Distinct blocks with reusable objects | Physical fixed region | Physical reusable region | CoreObject total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `tcp_rcv_state_process` | 410 | 343 / 67 | 79 | 504 B | 1208 B | 1712 B |
| `tcp_conn_request` | 260 | 213 / 47 | 42 | 480 B | 752 B | 1232 B |

The physical figures are **reserved frame regions**, not sums of IR
object payload bytes. For `tcp_rcv_state_process`, the fixed objects
contain 491 physical payload bytes plus alignment, while the 343
reusable objects occupy a much larger 1208-byte reserved region;
many of those objects each have use/def intervals wholly in one of
79 basic blocks. The current hotfix **forbids slot sharing across
different blocks**, so each block is assigned its own scratch space.

**Interpretation:** the dominant architectural opportunity is to
recycle *proven isolated and nonescaping single-block scratch objects*
across CFG blocks after rigorous alias and definition-before-use
verification. The naive opt-in cross-block proof previously failed
real P1 under more general settings and is not safe to promote.
Preceding this with dead IR/lowering object elimination reduces the
number of objects that have to be proven. Large unsafe objects,
address escapes, asm-goto, indirect labels, volatile memory and
read-before-write must stay on the prior independent-slot allocator.
An LLVM/GCC-like generic scalar SSA/regalloc upgrade is a much bigger
separate undertaking; do not hide it behind a "minor stack fix."

**Cost/benefit process:** (1) extract *measured* per-function frame
contribution and candidate deltas at known real GDB stack entries,
(2) rank by attainable bytes on actually nested 16KiB call paths and
semantic safety, (3) combine individually proven optimizations in a
single existing MiniC runtime profile, (4) use fast small C semantic
tests and one-TU frame/ABI A-B first, then unchanged-Image P1 3/3,
(5) certify full 2064-owner and other components only after reaching
a meaningful budget. Do **not** sum frames along a path unless one
live, ordered, same-run unwind proves they were simultaneously
resident; GDB breakpoint entry snapshots from different invocations
are not by themselves a full stack unwind.

**Decision:** stop broadening the trivial integer RHS predicate for
now. Its 12 fewer CoreObjects in `tcp_rcv_state_process` saved only
48 B of the 1776 B frame (~2.7%), 0 B in `tcp_conn_request`;
moving to a more general CFG-local scratch interference analysis is
the next large-leverage research objective. No new Workflow or branch.


## Two hot-owner dead-compare cleanup combined, original 16KiB — 2026-10-13

Targeted **only** `net/ipv6/ip6_output.o` and
`net/ipv4/tcp_input.o`, retaining the same 16KiB kernel and
the **2062 unmodified authenticated MiniC C objects**. Both modified
owners used the *existing* syntactically strict, side-effect-free
discarded local-address comparison elimination; no CFG-cross-block
slot sharing, no LLVM/GCC C owner, no new CI/workflow/branch.
CI **#38342452095**, artifact **11787631507**, completed the
actual selective recompile, original-object SHA/ELF checks,
linked a SHA-pinned Image and ran three independent standard
QEMU P1 boots.

| Modified owner / function | Original frame | Optimized frame | Saving |
| --- | ---: | ---: | ---: |
| `ip6_output.o: ip6_finish_output2` | 4064 | 2272 | 1792 |
| `ip6_output.o: ip6_xmit` | 2288 | 1520 | 768 |
| `ip6_output.o: ip6_output` | 768 | 512 | 256 |
| `tcp_input.o: tcp_conn_request` | 1312 | 1184 | 128 |
| `tcp_input.o: tcp_rcv_state_process` | 1776 | 1392 | 384 |
| **Sum of isolated function-frame reductions** | | | **3328** |

Do **not** treat 3328 bytes as a measured simultaneous stack
improvement: receive and transmit functions may not coexist
in one active call stack. All two optimized MiniC owners changed,
2062 others were authenticated identical.

P1 verdict of the exact linked Image
`44bd9ae156acae97bd13e191cf4a81844b47c74e547a7c79f426be27d25935ea`
was **FAIL/FAIL/FAIL (0/3)**; status green describes diagnostic
harness completion only. The initial all-MiniC 16KiB control still
failed at task-stack SP 144B below base in `__list_add_valid_or_report`.
The two-owner optimized Image's normal-QEMU panics moved:
1. `read_seqretry` called from `neigh_hh_output`, SP
   `0xff20000000000100` (256B above previously verified
   CPU0 IRQ base `0xff20000000000000`).
2–3. `__dev_queue_xmit+0x28`, SP
   `0xff1ffffffffffec0` (320B below that base), same
   IPv6 child-connect probe stage; P1 did **not** pass.

This is evidence that optimizing two high-ROI owners alone has not
yet eliminated the broader IRQ/network transmit stack pressure.
The original task-stack bottleneck was relieved enough to expose
a different network/IRQ failure manifestation. Do not infer the
specific new crash function is a miscompile (or that two
independent codegen defects have been identified) without a
matched call-stack/ABI proof. Inspect broad reusable scratch-object
pooling, high-impact nested frame budgets, and first-fault register
provenance before any further risky semantic optimization.
Original diagnostic script was restored byte-for-byte in the
same documenting commit; no unsafe patch was promoted.


### Whole-scratch-pool ROI screen (data-only, NOT a correctness certificate)

Offline re-read authenticated `intervals.tsv` records in
`p1-frame-11749646340.zip` (IPv6) and
`tcp-original-prologue-frames.zip` (TCP). The current backend
uses separate 8-byte scratch-slot pools for each basic block;
the per-block **maximum overlapping interval count** sums to the
actually reserved reusable slots. If every eligible nonescaping
single-block object's cross-block interference could be proven
absent, one shared scratch pool would need only the **largest**
per-block count. These are estimates of **reclaimable
allocation space**, not validated compiler optimizations:

| Function | Eligible local scalar objects | Local blocks | Current scratch slots | Max slots in any one block | Theoretical reclaim |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ip6_finish_output2` | 842 | 93 | 238 | 8 | 1840 B |
| `ip6_xmit` | 463 | 59 | 150 | 8 | 1136 B |
| `tcp_rcv_state_process` | 343 | 79 | 151 | 5 | 1168 B |
| `tcp_conn_request` | 213 | 42 | 94 | 7 | 696 B |

The `tcp_rcv_state_process` 1168-byte arithmetic exactly matches
the earlier experiment's 1776→608-byte candidate. That
corroborates the **allocator's savings mechanism**, NOT the
candidate's runtime correctness. The estimates use *baseline*
interval sets and **cannot simply be added** to prior dead
comparison savings because eliminating objects changes the
interval graph, frame alignment, and live regions. Furthermore,
large nested-function frame sums and aggregate reductions across
send/receive paths are not necessarily simultaneous.

The current priority is therefore a **verified semantic/liveness
argument and focused C regression probes** for reusable,
nonescaping, write-before-read block-local scratch; do not jump
from the large estimate to global cross-block slot aliasing.
A mere 48-byte integer no-spill expansion is quantitatively
lower ROI. The two-owner 3328-byte safe dead-comparison cleanup
is individually useful but demonstrably insufficient for 16KiB P1
on its own, as verified by #38342452095.


## Two-owner stacked high-ROI CFG scratch + pure-dead-compare — 2026-10-13

CI **#38344464421** (artifact **11787844020**) performed exactly
two selective Linux TU recompiles, `net/ipv6/ip6_output.o` and
`net/ipv4/tcp_input.o`, on the original **16KiB stack** kernel,
retaining 2062 original authenticated MiniC C objects and **no GCC C
objects**. Compiler codegen was modified **only temporarily** by the
existing strict pure-discarded-local-address equality elimination and
the existing first-write CFG-cross-block slot-sharing experiment,
explicitly scoped by function name to
`ip6_finish_output2` and `tcp_rcv_state_process`. All other functions
kept canonical CFG slot allocation. The chosen candidate classes are
small scalar, nonescaping, direct LOAD/STORE in one block, first actual
access a write; do not assume this heuristic is generally correct
without independent semantic testing. No Workflow or branch was added.

Frame measurements from actual emitted per-TU `CORE_FRAME_REUSE`
stderr, not guesses from deleted source expressions:

| Function | MiniC baseline | Combined candidate | Reduction |
| --- | ---: | ---: | ---: |
| `ip6_finish_output2` | 4064 B | **432 B** | **3632 B** |
| `ip6_xmit` | 2288 B | **1520 B** | **768 B** |
| `tcp_rcv_state_process` | 1776 B | **224 B** | **1552 B** |
| `tcp_conn_request` | 1312 B | **1184 B** | **128 B** |
| **Isolated frame delta sum** | | | **6080 B** |

The 6080-byte sum is NOT a real simultaneously resident stack budget:
TX `ip6_xmit` and RX `tcp_rcv_state_process` need not coexist on
the same nested call path. Both modified C objects passed ELF-ABI and
original-2062-object SHA isolation checks. Normal, **non-GDB** QEMU
P1 boots on ONE identical experimental Image
`459d04055f4220c59155c3b43b84a64c1cf3e930faba67baa1a4cc5938f0f57e`
were **FAIL / FAIL / FAIL (0/3)**:
* Boot 1: IPv6 `child-connect`, IRQ-stack guard overflow SP
  `0xff1ffffffffff0` (16 B below previously verified IRQ base);
  final site `update_vsyscall` → timekeeper helper.
* Boots 2–3: IPv6 `child-write`, same IRQ-stack boundary SP,
  near `__raw_spin_lock_irqsave`. The new Image was **not**
  independently GDB-attested, so site classification is corroborated
  against the earlier measured CPU0 IRQ-stack base, not a fresh
  in-situ unwind.
* Baseline unchanged 2064-MiniC P1 still failed at original
  **task-stack** guard, as expected.

GitHub job was **green only because the diagnostic completed**.
This is NOT a 16KiB P1 compiler fix, not proof of semantic safety for
CFG slot sharing, and not a production optimization approval. This
strongly constrains the assumption that these two owner units alone
are sufficient: even large verified local frame reductions do not
remove the remaining IRQ/network stack pressure in other TUs/callees.
An initial run **#38344056961** was red solely because an optional
`CORE_FRAME_COMPONENT` parser was used without its diagnostic
instrumentation. The corrected run consumed emitted
`CORE_FRAME_REUSE` records directly and completed correctly.

**Decision:** no further speculative two-owner QEMU rounds. Preserve
these large-ROI measurements, restore temporary scripts byte-for-byte,
then investigate the **actual remaining IRQ stack call-depth frontier**
(e.g., unchanged `tcp_v6_rcv`, `___slab_alloc`, `__alloc_pages`)
before attempting a general compiler backend optimization. Prefer a
semantic micro-test proof for CFG sharing first, not declaring success
from a shrunk frame alone.


## Receive/allocator CFG scratch ROI and actual 16KiB P1 — 2026-10-13

All trials used the existing frozen 2064-MiniC-object pool with the
original **16KiB** stack config, GNU AS/LD and zero GCC C substitutes.
The experimental codegen uses first-write, one-basic-block, direct
LOAD/STORE and no-escape eligibility for cross-block scratch sharing.
It remains **unproven for general semantic correctness**.

**ROI-only replay:** CI #38346387339, artifact #11789471374. Four
recompiled MiniC TUs: net/ipv6/tcp_ipv6.o, net/ipv4/tcp_input.o,
mm/slub.o and mm/page_alloc.o. All four ELF objects changed,
2060 other MiniC objects stayed authenticated unchanged. No Image link.

| Function | Original frame | Candidate | Delta |
| --- | ---: | ---: | ---: |
| tcp_v6_rcv | 1760 | 400 | 1360 |
| tcp_v6_do_rcv | 1104 | 480 | 624 |
| tcp_conn_request | 1312 | 624 | 688 |
| tcp_rcv_state_process | 1776 | 608 | 1168 |
| ___slab_alloc | 976 | 400 | 576 |
| __alloc_pages | 352 | 224 | 128 |
| __alloc_pages_slowpath | 1024 | 208 | 816 |
| **Sum (NOT simultaneous)** | | | **5360** |

**Four high-ROI owners linked:** CI #38346944943,
artifact #11789017206. Only ip6_output.o, tcp_ipv6.o,
tcp_input.o, slub.o were rebuilt with the experimental MiniC
backend. The other 2060 originals did not change.
Exact linked Image SHA256:
c66c8ae035a060c693c048761dcbde0073d25cd13ff9a6c14894e328a943f51d
Three independent ordinary-QEMU P1 boots: **PASS / FAIL / FAIL (1/3)**.
The failures were in IPv6 connect, both at SP ff1fffffffffffd0
(48 B below previously measured IRQ stack base); terminal functions
were an inline specialization from decay_load and __raw_spin_lock_irqsave.

**Five high-ROI owners linked:** CI #38347688537,
artifact #11789638848. Added mm/page_alloc.o as the **fifth**
selectively rebuilt MiniC C object, keeping other **2059** original
objects SHA-identical. The **same** Image SHA256:
f87e434c5678ebe5eace706c215a66423c39df1351e8b09288adb13ddb268eb9
Three normal-QEMU P1 boots: **PASS / PASS / FAIL (2/3)**.
Both PASS boots completed all 12 P1 syscall probes and clean shutdown.
Failing third boot crashed in IPv6 connect at
accumulate_sum+0x2 (caller ___update_load_sum+0x446), SP
ff1ffffffffff20 (224 B below previously measured CPU0 IRQ base).
This Image did not undergo its own fresh GDB unwind.

Measured five-owner frame reductions:
ip6_finish_output2 4064->2224 (-1840);
ip6_xmit 2288->1152 (-1136);
tcp_v6_rcv 1760->400 (-1360);
tcp_v6_do_rcv 1104->480 (-624);
tcp_conn_request 1312->624 (-688);
tcp_rcv_state_process 1776->608 (-1168);
___slab_alloc 976->400 (-576);
__alloc_pages 352->224 (-128);
__alloc_pages_slowpath 1024->208 (-816).
Aggregate **8336 B**, but these are independent functions from
possibly mutually exclusive TX/RX call chains, **not a measured
single live-stack saving**.

P1 progression (0/3 original -> 1/3 four-owner -> 2/3 five-owner)
supports accumulated stack pressure as the main performance/capacity
issue. It DOES NOT certify P1 or correctness of the CFG optimization.
The green GitHub diagnostic job statuses are NOT full pass claims.
Further broadening must include a Core IR liveness/alias proof,
focused small C correctness tests, and repeatably stable 16KiB P1.
Do not turn on unsafe global sharing just from this outcome.
Temporary opt-in experiment code is now removed, with the prior
canonical diagnostic script restored bit-for-bit. No Workflow or
branch was added and no production MiniC source was modified.


## 16KiB P1 milestone closure: seven-owner, twelve identical-Image passes (2026-10-13)

**Frozen success — no claim of globally safe optimizer promotion.**
Commit `0ec751c469216c1c5a4aae62c7bc83ec9ed74375`, Linux Runtime GCC Baseline
CI **#38351512579**, artifact **#11791865901**.
Default 16KiB task/IRQ stacks, Linux **6.6.143**, original complete
**2064-C-owner MiniC** inventory, GNU assembler and linker.
Exactly **seven** C owners rebuilt with the opt-in
`P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1` backend; the other **2057**
MiniC C owners preserve their manifest-verified original bytes,
**zero GNU C-owner substitutions**:

```text
net/ipv6/ip6_output.o
net/ipv6/tcp_ipv6.o
net/ipv4/tcp_input.o
mm/slub.o
mm/page_alloc.o
net/core/dev.o
kernel/time/hrtimer.o
```

Image SHA256 pinned to
`06bbd4e9bdd2c0ca4634ca55a7445c3565c8fc73cb00ce8bd39fc55d770a7530`.
Artifact `p1-defined-crossblock/p1-repeats.tsv` records **12/12 PASS**,
identical Image hash on all boots. Verified each of 12 raw
`trials/p1_defined_crossblock_N/qemu/rdinit-init.log` outputs has
`RUNTIME_V2_SYSCALL_END pass=12 fail=0`,
`INIT_RUNTIME_PROBE=PASS`, `reboot: Power down`, and no
`Insufficient stack space` marker. **12 repeated boots are 12
successful runs of the 12-probe P1 suite, not 144 independent distinct
test scenarios.**

A separate opt-in RV64 GNU-vs-experimental-MiniC differential executable
passed `P1_CFG_SEMANTIC=PASS vectors=255 cases=7 gcc_reference=same_process`
in the same CI run. Coverage includes branch chains, loop re-entry,
nested ternaries, goto, switch, volatile side effects, and pointer
escape. **These seven sample programs and 255 inputs do not prove CFG
cross-block CoreObject reuse safe for arbitrary C/IR or all 2064
owners.** The test exercises the experimental compiler, not a
permanently promoted default backend. Existing Linux runtime profile
and its inter-block-sharing prohibition stay intact.

### Reproducibility and limits

- Keep the **current existing opt-in seven-owner script** as the pinned
  replay mechanism while the semantic proof is incomplete; do not delete
  a working test or replace it with another Workflow.
- Stable 16KiB P1 is established for exactly the seven-owner compiled
  Image and tested configuration. It does not certify every runtime
  path, compiler correctness on untested inputs, entire toolchain
  native-linking, shell readiness, or self-hosting.
- The **other** workflow `Toolchain Focused Regression Suite`,
  #38351512614, is **RED**, independent of the P1 success. Its default
  (not experimental-profile) RV64 focused job fails while linking
  `run-constant-if-link-v0.sh`:
  `undefined reference to missing_arch_dma_set_uncached`.
  MiniAR/MiniLD checks succeeded. Record this as a separate
  unclosed regression; do not claim global CI is green or attribute
  this symbol to CFG reuse without causal evidence.

### Next phase — correctness gate before promoting any global stack optimization

1. **Freeze** this pinned P1/Image/object manifest. No more repeated
   whole-Linux rebuilds merely to search for smaller frames. Reuse
   the same existing CI receiver and frozen-owner mechanism.
2. **Prove cross-block slot soundness** in the actual Core IR:
   enumerate every OBJECT_ADDRESS and direct/indirect use, escaped
   address, all predecessors and backedges, reads-before-writes,
   loop re-entries, goto/asm-goto and aliasing. Verify that shared
   stack slots cannot represent simultaneously live values on
   any reachable path. Current "first use is a STORE" is evidence
   of an opportunity, **not** a general interference proof.
3. Expand differential testing with focused adversarial C cases and
   Linux-owner samples; compare observable outputs and side effects
   with GNU. Fix only proven unsound reuse rules. Do not broaden the
   optimization beyond this seven-owner experimental cohort until
   those checks pass.
4. Resolve the unrelated default RV64
   `missing_arch_dma_set_uncached` regression independently using its
   existing focused test, avoiding unrelated CI/workflow changes.
5. After correctness gates, promote a conservative general CoreObject
   optimization behind an explicit switch, measure stack/compile-time
   changes on same hardware, then test all-MiniC Linux P1 and later
   shell/whole-toolchain replacement/self-hosting. Dedicated space and
   speed optimization follows after runtime correctness stabilizes.

**Repository hygiene at milestone:** no extra branch or Workflow
needed; no production compiler code modification is authorized by
this result. This appended milestone is the only intended persistent
change in the closing commit.
