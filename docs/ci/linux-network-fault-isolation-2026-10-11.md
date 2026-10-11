# Linux network fault isolation (11 October 2026)

Pinned kernel: Linux 6.6.143, RISC-V virt, same GCC golden tree, 2064 MiniC C objects. Earlier `earlycon.o` and `fs/super.o` have been rebuilt with corrected MiniC.

## Causal GCC swaps (run 38101642461)

- `net/ipv4/tcp_cong.o`: precisely one `__minic_inline_spec_6515_19` definition, also defining `__get_unaligned_cpu32`. GCC substitution removed the kernel paging fault and PID1 panic. Kernel progressed to PCI / serial driver initialization. Netlink warnings remained.
- `net/netlink/genetlink.o`: GCC substitution removed `genl_op_from_split` WARN_ON at upstream `net/netlink/genetlink.c:236`. The same paging fault remained.
- Both swaps preserved all other verified C owners and reconciled the four generated vDSO-header-dependent owners. Neither is full P1 certification.

## Current MiniC replay (run 38102104446)

- With unchanged compiler profile, both network object SHA256 digests were identical to the old MiniC pool and both faults remained.

## Rejected optimization hypothesis (run 38102595764)

The experimental local-fact barrier for integer compound assignments and ++/-- changed the `genetlink.o` bytes from `753df430...` to `c2e5a316...` but **did not change `tcp_cong.o`**, whose original hash starts `9be2d853...`. Focused toolchain regression passed. This mixed Image did not reach Linux's UART banner; QMP observed an early instruction-page-fault state. Thus this profile modification did **not** establish a fix. The experimental barrier has been disabled again in the production runtime profile.

## Next diagnostic

Archive GNU-pinned `.i`, current MiniC `.s`, and side-by-side GCC/MiniC object disassembly for only these two translation units, without a GNU kernel relink. Identify the exact bad loop condition/operation lowering before modifying the compiler again.

## Exact assembly root cause, 11 October 2026

The GNU-preprocessed jhash code in tcp_cong.o compiled into __minic_inline_spec_6515_19. Original MiniC assembly initializes specialized length to 16 and performs length -= 12 in the loop body, but the generated function has zero conditional branches: its backedge is unconditional. It can read indefinitely past the key until faulting in __get_unaligned_cpu32. The new candidate barrier invalidates only specialized parameters that this loop body may modify, before lowering the condition. Confirm against exact TU before any full kernel relink.

## Confirmed jhash CFG repair and subsequent boot (runs 38103374046, 38103535871)

- Updated `tcp_cong.o` SHA256 `2bfa89b450a3...`; the previously unconditional specialized `__minic_inline_spec_6515_19` loop now contains runtime conditional branches and an exit path. `genetlink.o` remained unchanged.
- Four objects rebuilt by the current MiniC profile (`earlycon.o`, `fs/super.o`, `tcp_cong.o`, `genetlink.o`); 2060 remaining MiniC objects were verified unchanged.
- Exact QEMU UART evidence: the earlier `__get_unaligned_cpu32` page fault and attempted-to-kill-init panic did not recur. The kernel reached PCI initialization, freed initrd memory at ~7.48 kernel seconds, and announced the 8250/16550 serial driver at ~11.71 kernel seconds. It did **not** prove PID1 handoff or P1, and the `genetlink.c:236` WARN_ON remained.
- This separation is important: kernel execution advanced, but absence of panic inside a bounded window is not equivalent to a passing boot certificate.

## Netlink's distinct warning and regression

- Actual Linux 6.6.143 preprocessed source (exact `genetlink.i`): `({ int __ret_warn_on = !!(!cnt); if (__builtin_expect(!!(__ret_warn_on), 0)) do { asm volatile("ebreak"); } while (0); ... })`.
- Actual MiniC `genl_op_from_split` assembly has `seqz` conditions followed by a **fallthrough, unconditional** `ebreak` at the head of `.Lgenl_op_from_split_core_bb6`, without the C-required branch gate. A GCC-only replacement of `genetlink.o` suppressed this warning independently.
- Added `tests/compiler/c0/netlink_warn_guard_runtime.c` and wired it to `tests/compiler/c0/run-runtime.sh`, retaining GNU statement-expression, `__builtin_expect`, `do/while(0)` and a guarded `ebreak`. Host-compiler control-flow semantics were checked, but the actual patched MiniC/RISC-V runtime case has **not yet been shown to pass** by a CI run.
- Next focus: determine why the local integer fact/statement-expression conditional reaching the inline-assembly warning body was folded into a fallthrough path. Avoid broad runtime profile changes or another full-link experiment until the isolated test and genuine `genetlink.s` branch gate are fixed.
