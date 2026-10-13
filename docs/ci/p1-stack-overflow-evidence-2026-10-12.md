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
