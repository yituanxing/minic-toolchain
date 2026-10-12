# Linux P1: Task-vs-IRQ stack diagnosis (2026-10-12)

## Established frozen evidence

- Original all-MiniC 2064 Image `6bc56383ea7022f7...`: P1 fails at
  `__list_add_valid_or_report+0x1c`, trapped SP
  `ff200000001cff70`, task stack
  `[ff200000001d0000..ff200000001d4000]`; **144 bytes below
  the task stack lower bound**. This is a plausible ordinary task-stack overflow.
- The separate opt-in experiment changed **one** MiniC object
  `net/ipv6/ip6_output.o`, with `net/ipv6/tcp_ipv6.o` supplied by
  GCC (2063 MiniC + 1 GCC). Three reboots of **exactly the same Image**
  `d320f24bdb2633...` failed at different EPCs. Trapped SPs:
  `ff1fffffffffffa0`, `ff1ffffffffffffe0`, `ff1fffffffffff50`.
- The guest prints vmalloc start `ff20000000000000`:
  those SPs are **96, 32, 176 bytes below vmalloc start** respectively.
  They are *not* approximately 1.9 MB below a presumed IRQ-stack base.
  The previous `sp_below_stack_bytes` fingerprint always subtracted the
  task-stack low bound even when SP was unrelated to that task stack;
  that calculation must not be interpreted as overflow depth.
- Every failure happens after child `connect()` in the same P1 IPv6
  oracle. Different EPCs alone do not demonstrate distinct root causes.
- Kernel 6.6.143 RISC-V `handle_bad_stack` reports
  `current->stack` and the emergency overflow stack, not the
  independent per-CPU IRQ stack base. For `CONFIG_IRQ_STACKS`,
  `do_irq` can switch from the task stack to `irq_stack_ptr`
  allocated under `CONFIG_VMAP_STACK`.

## Diagnostic (no compiler change)

The `[linux-runtime-stack-owners] [linux-runtime-gdb-stack]`
opt-in reuses the immutable full 2064-MiniC **failing** Image and
matches its `vmlinux`. The probe waits for `tcp_v6_connect` and then
reads CPU0 `__per_cpu_offset[0]` and the relocated per-CPU
`irq_stack_ptr` value using GDB. These are the actual per-CPU values,
not guessed from `current->stack`.

After the TCP gate it records `do_irq`, `handle_riscv_irq`,
`hrtimer_interrupt`, red-black-tree, and list-debug entry SP values,
alongside both real stack lower bounds. An emergency-stack breakpoint
is classified separately. Serial logs retain the saved exception
registers. The original stack-fingerprint parser now only reports a
numeric task-stack-underflow distance for an SP at most 4096 bytes below
the *task* stack low bound, and emits an unverified vmalloc-boundary
candidate label for the opt-in crashes.

## Evidence gate / decisions

1. Require `P1_GDB_IRQ_LAYOUT` and a nonzero,
   plausible `irq_stack_ptr` from CPU0 per-CPU storage.
2. Verify a post-`tcp_v6_connect` IRQ-handler SP falls within
   `[irq_stack_ptr, irq_stack_ptr + 16384)`. If not, the candidate
   IRQ-underflow hypothesis is **unverified**, not promoted.
3. Only then compare the trapped SP in the **same** uninstrumented Image
   serial log to that IRQ-stack base; report overflow distance.
4. Use entry SPs and ABI prologues to identify the specific stack
   exhaustion path; if SP is not consistent with either stack, switch
   to first-register-corruption diagnosis.
5. GDB changes timing. Its observations are diagnostic evidence only;
   only a normal, non-GDB P1 oracle counts as PASS.

No new whole-Linux rebuild and no speculative MiniC codegen optimization
are justified before the stack identity is established.


## Verified after opt-in GDB run #38265238942 (actual CI, 2026-10-12)

The new **all-MiniC failed control** was reproduced, without codegen changes:
`p1_full_minic` 2064/2064 MiniC, `FAIL`, Image SHA
`3f372cca352329df66eab389365874a0fb72244377027d245d30d41c6ac6657d`.
The GDB run used that **same Image**, with the same matched `vmlinux`;
`P1_GDB=CAPTURED`.

Directly read at the `tcp_v6_connect` breakpoint from CPU0's relocated
per-CPU `irq_stack_ptr`:

```text
irq_base = ff20000000000000
irq_top  = ff20000000004000
task_lo  = ff200000001c8000   (this run's PID 40 task)
```

The real stack switch was observed: `do_irq` entered on the **task**
stack (`SP=ff200000001cada0`), then `handle_riscv_irq` entered on
the **IRQ** stack (`SP=ff20000000004000`). Subsequent IRQ entries:
`hrtimer_interrupt` entered at `irq_base+15680`,
`rb_erase` at `irq_base+14544`. The same `__list_add_valid_or_report`
was seen running on both task and IRQ stacks: a function name alone
does not tell us which stack is in use.

Last original failing **task**-stack path in the instrumented run:

| Function entry | Task-stack bytes left |
| --- | ---: |
| `tcp_v6_connect` | 14,448 |
| `ip6_xmit` | 11,184 |
| `ip6_finish_output2` | 7,088 |
| `__list_add_valid_or_report` (earlier call) | 2,080 |
| `__list_add_valid_or_report` (last call) | **224** |

The last function's checked MiniC prologue subtracts 368 bytes,
so the resulting saved exception SP is **144 bytes below task stack
base**. The GDB run caught the actual `handle_bad_stack` handler on
the emergency stack, while the separate **ordinary** (no GDB)
run independently failed at the same `__list_add_valid_or_report`
boundary with task SP `ff200000001c7f70`.

**Conclusion supported now:** the original all-MiniC P1 failure is
an actual 16 KiB **task-stack underflow** along IPv6 connect; task-SP
corruption is not needed to explain this immediate fault. This does
**not** yet rule out an upstream miscompile, recursion, or an
independent IRQ-stack failure. The previously seen three SPs
`96/32/176` bytes below `vmalloc_start` exactly match the
**IRQ-stack base observed in this control image**, but those SPs came
from a **different experimental Image**. To promote that inference
to proof, run equivalent read-only GDB on the experimental image,
or independently certify that image's IRQ-stack allocation.

Do not conflate original (all-MiniC, task stack) and experimental
(2063 MiniC + 1 GCC, vmalloc boundary) failure mechanisms. Do not
promote the GDB-observed image as P1 PASS. The next most targeted
investigation is the exact task-call-chain prologues causing the drop
from 7,088 to 224 bytes, then, separately, the modified image's IRQ
stack allocation and call-chain.


## Second CI confirmation: the experimental Image has a **real IRQ-stack underflow**

Diagnostic run **#38266372041**, commit `32a2008c5feb...`,
reconstructed the same opt-in experimental profile as the earlier
IPv6 trial: exactly one newly compiled MiniC `ip6_output.o`, one GCC
`tcp_ipv6.o`, and the other 2062 unchanged MiniC objects.

- P1 normal reboots: `FAIL` **0/3**; the **same** SHA256 Image
  `7453a45063bb7d608ec40abc3b5cf0573159b5ee65b6f84990432d71f206f949`
  was used for all three.
- Under that **same experimental Image**, a separate GDB reboot read
  `irq_stack_ptr` through `__per_cpu_offset[0]` and verified the actual
  IRQ-stack range:
  `[ff20000000000000 .. ff20000000004000)` (16 KiB).
  GDB also observed the IRQ-handler's entry SP within this range.
- The 3 uninstrumented P1 failures saved trapped SPs below the **verified
  IRQ base by exactly 128, 32, and 32 bytes**. The independent correlation
  produces `P1_DEFINED_CROSSBLOCK_IRQ=VERIFIED` and a dedicated
  `p1-defined-ip6-only/p1-irq-attribution.tsv` in the uploaded CI artifact.
- A GDB-sampled IRQ call path shows `hrtimer_interrupt` entering
  with 9,200 bytes remaining, `rb_erase` with 8,064,
  `ip6_xmit` with 7,936 and `ip6_finish_output2` with 4,608.
  Those values are **entry SPs** observed in a perturbed GDB run;
  they do not alone identify the lowest SP or an erroneous codegen rule.
- The opt-in IPv6 frame sizes were confirmed as
  `ip6_xmit=1520 B` and `ip6_finish_output2=2272 B`.
  Reducing both was **insufficient** to pass the original P1 oracle.

**What is now established:** the all-MiniC 2064 control can exhaust a
16 KiB **task** stack, and the modified IPv6/GCC one-owner experimental
configuration can exhaust the real 16 KiB **IRQ** stack. Both are backed
by Linux's saved exception SP and direct per-CPU GDB stack-base
measurement. The two observed overflow mechanisms should no longer
be confused with an arbitrary 1.9 MB SP corruption.

**What remains unproven:** these two exceptions by themselves do not
prove every upstream instruction is correct, that no spurious recursion
exists, or that the only solution is stack-slot optimization. They prove
that stack capacity is the immediate fatal boundary in both tested
profiles, not why the active call-chain has that depth.

**Next diagnostic priority:** capture the immediate call/prologue chain
*before* the low SP for the task/IRQ contexts, distinguishing true frame
sizes from accidental recursion or register corruption. Do not turn
the successful GitHub workflow status for this *diagnostic* into a false
Linux P1 PASS.


## Frozen all-MiniC first-fault capture — CI #38269165512

A **new all-MiniC** 2064-object Image
`2fe1f432e2e80b3189acb1f4955c413c7e24364ca2264f7d0a329c12a5610645`
failed normal no-GDB P1 with saved `arch_local_irq_restore` EPC and SP only
96 bytes **above** the task stack base. A subsequent GDB run under the
**same immutable Image** reached `handle_bad_stack` and captured raw
RISC-V `pt_regs` *at the emergency handler entry* (rather than
interpreting the handler's own SP as the original fault).

Its first fault was:
```text
epc     = do_irq+0x20
ra      = _save_context+0x86
cause   = 0xf (store/page fault)
badaddr = ff200000001c7fd0
sp      = ff200000001c7fd0
task_lo = ff200000001c8000   (48 B below lower bound)
irq_lo  = ff20000000000000
irq_hi  = ff20000000004000
```

This is **task-stack exhaustion during an interrupt/exception**, before
the IRQ handler can safely finish transitioning to its separate IRQ stack.
The immediate failing instruction/trigger varies with interleaving:
previous runs failed during the list-add path; this time task stack
was sufficiently exhausted that saving/handling an interrupt crossed
its vmalloc guard. Both scenarios are consistent with the measured
excessive nested MiniC stack budget, but **not** enough to establish
which compiler lifetime rule is responsible.

New CI artifact saves exact `pt_regs`, both 16 KiB stack snapshots,
a 13 MiB linked `nm` symbol map, and a symbolized **candidate-only**
stack-word table, under `p1-gdb-stack/`. Recovered candidate words
near task-stack floor reference `get_freepointer_safe`,
`__slab_alloc_node`, `__kmalloc`, `neigh_alloc`, and
`___neigh_create`; farther up are IPv6 output and connect frames.
Candidate text pointers can also be locals/constants: **not a proof of
the exact call sequence or recursion**.

The next no-codegen-change focused GDB run adds selective low-stack
breakpoints on neighbor allocation and slab allocators, to measure
true **function-entry SP** near the fault without treating stale stack
words as a call trace.

Do not infer Linux P1 PASS from success of the diagnostic CI workflow.
