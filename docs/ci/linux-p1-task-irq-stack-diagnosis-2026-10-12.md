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
