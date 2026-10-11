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
