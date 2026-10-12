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
