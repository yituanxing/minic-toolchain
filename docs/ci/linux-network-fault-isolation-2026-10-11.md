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

## Confirmed Netlink dominance-bug fix (11 October 2026)

### Source and compiler mechanism

- Reproducing exact `genetlink.i` under the production MiniC profile showed the first two `genl_op_from_split()` conditions as true Core conditional branches; the third `if (__builtin_expect(!!(__ret_warn_on), 0))` was previously generated as an unconditional jump to the `ebreak` block.
- The actual **post-patch** Core implementation, archived in run `38109732757` as `genetlink.core-branch-impl.c`, proved the missing branch was a `core_const_eval_integer_with_locals()` early return in `lower_condition_branch()`. Its generic if caller builds THEN/ELSE bodies before lowering the earlier condition block. Mutable facts learned inside the THEN branch do not dominate the entry condition.
- Fix commit `b7a489c61c62b3be228f83c6b07473880049f4ce`: apply `tools/ci/apply-core-if-condition-dominance-barrier-v0.py` as part of the exact runtime compiler profile. In a dynamic `lower_if`, clear speculative local facts **after** processing the branch bodies, **before** emitting the condition-block terminator. Compile-time constant branch pruning still happens earlier and remains intact.
- Run `38109926916` confirmed real TU `genetlink.o` changed to SHA256 `421bf6ebd681f2f1f7ee7934611ac329d20fb1a646c058ae2fefd9779770cc39`. Core now emits the third predicate, including the conversion / logical-NOT / local read chain, instead of taking its constant-fold return. The generated assembler block `.Lgenl_op_from_split_core_bb6` contains a `beqz` gate: false branches to `bb9`, true enters the `bb8` warning/ebreak block.

### Authentic four-object Linux QEMU evidence

- Run `38110120236`, commit `a26a6ec17535d471e81d70e159a273c881a00942`, rebuilt precisely `earlycon.o`, `fs/super.o`, `net/ipv4/tcp_cong.o`, and `net/netlink/genetlink.o` with the current MiniC; integrity check verified 2060 other frozen MiniC objects unchanged. The GNU linker and both QEMU diagnostic variants ran successfully as CI operations.
- Both archived `qemu-uart-earlycon.log` and `qemu-explicit-fast/rdinit-init.log` compared against pre-fix run `38103535871`:
  - `genl_op_from_split` stack-name appearances **20 → 0** per log.
  - `net/netlink/genetlink.c:236` **5 → 0** per log.
  - `WARNING:` **5 → 0** per log.
  - `Unable to handle kernel paging request` and `Kernel panic` **0 → 0** per log (earlier jhash bug remains absent).
- The explicit-initrd boot reached `Freeing initrd memory: 836K` (guest t≈6.73s) and `Serial: 8250/16550 driver, 4 ports, IRQ sharing disabled` (guest t≈9.56s). The bounded run was terminated by the diagnostic timeout; there was **no observed `Run /init as init process`**, so full boot/P1 remains **NOT CERTIFIED**. Do not represent CI job SUCCESS as successful Linux PID1 startup.
- The MiniC/RISC-V isolated `netlink_warn_guard_runtime.c` runnable regression was added but has not yet been explicitly executed by a matching runtime test job; the stronger real-TU assembly and mixed-kernel QEMU differential are confirmed.

### Next frontier

Focus only on the first post-8250 stall/slow-init boundary: extend existing QMP/GDB function+PC sampling around and after guest `Serial: 8250/16550 driver`, distinguish forward progress from a loop, then isolate one owner at a time with same-cache GCC swaps. Do not expand workflows or perform another 2064-object rebuild.


## Recovered Python 145/203 runtime test: first real post-module C Image fault

The original Python V024 Linux 6.6.143 `Image` (SHA256
`7c868f2479c1c14c40f442236bb3c62cc530196b581104d0eaa2ef8401b8bb30`)
with the authenticated original natural145-v024-cold-overlay.cpio achieved 145/145
`MODULE_PASS` plus 203/203 `FUNCTION_PASS`, QEMU exit 0, normal guest shutdown,
and complete exact unique-ID matching.

The 7-owner-repaired current MiniC Linux Image SHA256
`791e2ec22f9f6a5cf02eb7f437c6cf25e5e8e89e6fa8230ce6573f0749beb056`,
built by the current GNU-locked Kbuild pool and independently exported from
run `38126738736`, was run against that **same unmodified old cpio**, with
the same QEMU host, 1 GiB RAM, explicit initrd, `rdinit=/init` and the
historical no-watchdog kernel arguments.

Actual results (two QEMU reproductions):

- **145/145 module loads PASS**, including Btrfs, KVM, Radeon, BLAKE2b, Nouveau.
- **110/203 functional PASS**, matching the first 110 historical functional IDs.
- The last successful assertion is `FUNCTION_PASS iptables-module-chain`.
- Historical assertion #111, `iptables-get-info`, fails before emitting PASS.
- First exception: `Unable to handle kernel paging request` with
  `epc: nf_hook_entries_grow+0xd86/0x1096`, `ra: nf_hook_entries_grow+0x7b4/0x1096`,
  `badaddr: ff60000011790000`, `cause: 0xf` (store page fault).
- Call chain: `iptable_filter_table_init` -> `xt_find_table_lock` ->
  `xt_request_find_table_lock` -> `get_info` -> `do_ipt_get_ctl` ->
  `nf_getsockopt` -> `nf_hook_entries_grow`.
- The first oops is followed by a secondary IRQ/timer oops and kernel panic.
- Same faulting function and PC offset reproduced on the second QEMU run,
  ruling out a mere timeout or Nouveau slowdown.

Upstream `nf_hook_entries_grow` is implemented in
`net/netfilter/core.c` -> `net/netfilter/core.o` (candidate owner, not yet
causally certified). It counts and copies/extends Netfilter hook arrays, and
the bad instruction attempts a store to an invalid address.

**Next deterministic isolator**: hold the entire current repaired MiniC Image
object set and linker context fixed; restore the pure-GCC version of precisely
`net/netfilter/core.o`, relink and rerun the historical unchanged cpio.
If the failure frontier advances past `iptables-get-info`, regenerate this
one source from the identical pinned `.i` and inspect GCC vs MiniC
IR/assembly for hook count, pointer indexing, and loop join behavior. If it
does not, search interacting owners before asserting an isolated compiler
fault. The historical Python reference Image is not itself a strict
same-config control; use the pinned exact-config GCC golden pool for attribution.

This is a **confirmed post-145 runtime regression**, not P3 PASS, and neither
the crashing image nor the original historical binary archive should be
silently promoted to a release fixture.


## Original Python 145/203 oracle: exact one-GCC-object causal replay (run 38129015401)

The original 67,667,456-byte V024 cpio stays byte-identical across this paired
test. The seven-owner-repaired MiniC 2064-object candidate was rebuilt and
linked by GNU Kbuild, then a second independent trial replaced **only**
`net/netfilter/core.o` (universe zero-based index 1931) with its certified
same-config GCC object. The trial verifier checked 2063 remaining MiniC C
owners and the 40 derived/conditional GNU transformations.

- MiniC-only linked Image SHA256 `a06b11fa47c284ccfc1441e8509015d5e2878d9dc945b0235953e3170d56581c` (215,502,848 bytes, current replay). Its historical first fault on the unchanged cpio had 145 module passes, **110 function passes**, then `nf_hook_entries_grow+0xd86` store page fault at `0xff60000011790000`.
- One-GCC `core.o` Image SHA256 `5c612e95e3aa0de047eceb1856f7273f9c11a82185b9afdba0419a130bb475c4` (217,600,880 bytes). In genuine offline QEMU with the **same** original cpio, 145/145 MODULE_PASS plus **186 FUNCTION_PASS**. In particular `iptables-get-info` (#111) and downstream networking tests progressed past the original fault.
- A **different** kernel fault occurred next, immediately after
  `FUNCTION_PASS crypto-concrete-algorithms-registered` (#186):
  `_compound_head+0x40/0x160`, `ra=__minic_inline_spec_9309_155+0xbc/0x4f6`,
  invalid virtual address `0x9`, `cause=0xd` (load page fault), followed by
  `Kernel panic: Attempted to kill init`. Keep separate from the Netfilter
  crash; do not conflate with 145/203 PASS.
- A focused static GNU userland reproducer,
  `tests/external/linux/netfilter_getinfo_runtime_probe.c`, has also been
  built and tested with the **exact historical** `x_tables.ko`,
  `ip_tables.ko`, `iptable_filter.ko`. On Python V024 Image, MiniC Image
  and the one-GCC-core Image, basic `IPT_SO_GET_INFO` passes.
- The focused probe now accepts `netfilter_upto=N` on the kernel command line
  to reproduce the first 39 old module registrations (and selected later
  Netfilter owners) in authentic historical order without changing the original
  final-acceptance cpio. GNU-reference prefix39 and MiniC prefix39 each
  return `GETINFO_PASS hooks=0xe entries=4 size=632`.
  **Therefore the first fault is state-dependent and is not reproduced by
  just loading the first 39 modules.**

### Direct machine-code evidence for the original first fault

The currently exported 215,502,848-byte MiniC Image was disassembled as
RISC-V RV64 from the exact panic PC. At
`ffffffff8613d51a` inside `nf_hook_entries_grow`, the faulting opcode
`0062b023` is `sd t1,0(t0)`: a write to the pointer calculated from
a hook array base plus `nhooks * 16 + 8` (the hook's `priv` field).
The in-function RV64 loop guard at `ffffffff8613d030` genuinely performs
unsigned `i < old_entries`; do **not** assume an unconditional-backedge
bug without inspecting the full data flow.
The exact invalid `t0=ff60000011790000` shows the destination address is
outside mapped memory, but does not by itself prove whether hook counts,
allocation length, or earlier state was wrong.

This is a verified GCC-owner causal experiment and a strong direction for
isolating the MiniC miscompile. **A compiler semantic fix has not yet been
validated**. Promote neither the GCC swap nor the 186/203 truncated result as
a MiniC P3 PASS.

## Completed exact-config GCC core.o swap, original Python 145/203 suite

The single-GCC-owner swap was **executed** in GitHub Actions
[#38129015401](https://github.com/yituanxing/minic-toolchain/actions/runs/38129015401).
CI recorded `NEXT_OWNER_SYMBOL=CONFIRMED symbol=nf_hook_entries_grow
owner=net/netfilter/core.o`, index **1931** in the fixed 2064-item universe,
and verified unchanged/non-selected ELF derivations.
One-GCC swap Image SHA256:
`5c612e95e3aa0de047eceb1856f7273f9c11a82185b9afdba0419a130bb475c4`,
size 217,600,880 bytes. CI artifact 11698147847 contains the exact linked
Image and identities. This was re-run with the **unmodified** original V024
natural145 `cpio` in the same portable QEMU environment as the seven-repair
MiniC-only Image.

| Oracle | Seven-repair MiniC | GCC only net/netfilter/core.o |
| --- | --- | --- |
| MODULE_PASS | 145 / 145 | 145 / 145 |
| FUNCTION_PASS | 110 / 203 | **186 / 203** |
| Last function | iptables-module-chain | crypto-concrete-algorithms-registered |
| First fault | nf_hook_entries_grow+0xd86 | **_compound_head+0x40** |
| Complete terminal endpoint | No | No |

In the GCC-owner variant, the original Netfilter page fault **did not recur**,
and the old `iptables-get-info` check and subsequent tests succeeded. The
second fault is a load page fault (`cause=0xd`), `badaddr=0x9`,
`epc=_compound_head+0x40/0x160`, and
`ra=__minic_inline_spec_9309_155+0xbc/0x4f6`, PID 1.
It follows immediately after `FUNCTION_PASS crypto-concrete-algorithms-registered`
and before the historical expected `FUNCTION_PASS afalg-sha256-abc`.
The kernel panics after PID1 dies.

**Inference level:** causal role of the MiniC-built `net/netfilter/core.o` in
the earlier Netfilter failure is strongly established by exact-config one-object
differential. The lower-level compiler miscompile inside this object is not yet
fixed. The later `_compound_head` fault is a distinct observed runtime
regression; it is *not* proven to be an `mm/` object miscompile by the faulting
PC alone. A bad pointer may be passed from the caller and must be traced.

**Next work:** inspect `nf_hook_entries_grow` MiniC IR and the precise store
at fault PC, which corresponds to hook-array pointer/indexing; establish a
focused GCC/MiniC semantic differential and fix the compiler, then regenerate
only `core.o` against the pinned .i. For the second fault, resolve the
`__minic_inline_spec_9309_155` caller owner by its actual vmlinux symbol map
and test an independent exact-config GCC swap. Retain the full unmodified
Python-era 145/203 cpio as the final runtime oracle.


## Authentic eighth-owner Netfilter MiniC repair: QEMU 145/186, second fault isolated

The first Netfilter fault is now fixed by **recompiling the exact GNU-preprocessed
`net/netfilter/core.o` with the current patched MiniC**, NOT by borrowing GCC
code. Candidate Linux Image SHA256:
`a7e28c5512e83a3323158c0aa2933a8820934bbf1411954e4149dcfe8dfc5cb1`
(215,502,848 bytes). CI run
[#38132238084](https://github.com/yituanxing/minic-toolchain/actions/runs/38132238084)
certified `NETFILTER_CORE_ABI=PASS`,
`NETFILTER_CORE_FROZEN_NEIGHBORS=PASS count=2056` and
`NETFILTER_CORE_CURRENT=READY old=a3da93e956e5... new=5329664f102245...`.
The current-profile MiniC assembly for `nf_hook_entries_grow`
reinstates a runtime read of the mutable `inserted` local and a `beqz`
guard where the previously frozen bad object had a direct unconditional edge.
This is consistent with the previously added compiler-wide
`apply-core-if-condition-dominance-barrier-v0.py` repair.

**Independent offline QEMU full historical oracle**, using the **unchanged**
Python V024 145/203 cpio and exact same QEMU boot parameters:

- 145/145 original module PASS, including Nouveau.
- 186/203 original function PASS, **including** all the Netfilter
  `iptables-get-info` gates that crashed at function #111 previously.
- The former `nf_hook_entries_grow+0xd86` store page fault no longer occurs.
- The following previously observed `_compound_head+0x40` load fault at
  `badaddr=0x9` **still occurs**, after function #186. The fully intact
  historical acceptance gate remains FAIL / not complete, regardless of the
  direct QEMU host exit status.

The sequential source identity assures **2056 untouched neighboring MiniC
C objects**, and a newly compiled MiniC `core.o`. This is not a GNU
substitution. The same before/after behavior as the GCC-only swap
(145/110 -> 145/186) independently supports the generic condition-join
semantics repair.

**Second bug owner discovery while QEMU ran**: a complete
`riscv64-linux-gnu-nm -A --defined-only` scan of the authenticated frozen
2064 MiniC objects in CI run #38133195948 found exactly
`mm/gup.o:0000000000021ca2 t __minic_inline_spec_9309_155`,
which is the return-address function shown at the second fault.
This is a verified *call-site object identity*, not yet proof the object is
the ultimate source of the malformed `struct page *` pointer.
Investigate its exact current-profile translation and data flow before
changing memory-manager internals. Re-test the original P3 oracle after
every proven compiler repair rather than stockpiling object swaps.


## Ninth-owner MiniC repair: complete original Python 145/203 QEMU PASS (11 October 2026)

**Exact real offline QEMU replay**, using the authenticated original, unchanged
Python V024 guest test archive and the latest **nine repaired MiniC owners**
Image exported by GitHub CI
[#38135950731](https://github.com/yituanxing/minic-toolchain/actions/runs/38135950731),
artifact `11700219984`. Runtime validated in the local portable QEMU 8.2.2
(recovered from CI's host bundle), RISC-V virt, one CPU, 1024 MiB RAM,
explicit non-overlapping initrd placement via DTB, original
`rdinit=/init` plus the legacy no-watchdog kernel parameters.

- **Linux Image:** SHA256
  `8e2a6b39a2b90107dd1c4093bea88d229806812ffb344cea03d2dd855910e0f5`.
- **Unchanged Python-era initramfs:** SHA256
  `88c81f1b9eaa3377fbed3f99f29d34c60491eede6ccd57966a609b16b619a51b`.
- **145 MODULE_PASS / 145 expected**, distinct IDs 145, entire ID order
  byte-for-byte identical to the preserved original successful QEMU log.
- **203 FUNCTION_PASS / 203 expected**, distinct IDs 203, entire ID order
  byte-for-byte identical to the preserved original successful QEMU log.
- The final `MODULE_PASS nouveau` and `FUNCTION_PASS bridge-links-up`
  are observed.
- **Terminal assertion:** `M38_FINAL145_V028_END pass=203 fail=0`.
- **Normal guest poweroff:** `reboot: Power down`, last guest timestamp
  approximately 543.33 seconds.
- **0 observed** kernel Oops, BUG, paging faults, soft lockups, kernel panics,
  unknown module symbols, module/functional FAIL markers.
- The `nf_hook_entries_grow` (#111) and `_compound_head` (#187)
  prior failure frontiers are both crossed in the new **MiniC-only**
  repaired owner set; no one-off GCC object swap was retained in this Image.
- CI confirmed the ninth owner `mm/gup.o` was rebuilt via the current MiniC
  profile and preserved **2055 immutable frozen MiniC neighbors**:
  `GUP_CURRENT_FROZEN_NEIGHBORS=PASS count=2055`.

**Evidence boundary:** this proves the precise mixed-generation MiniC
2064-C-object Linux Image passes the *original Python V024 historical*
145-module / 203-function runtime oracle. It does not yet certify a
fresh full-current-MiniC compiler build of every object, the P1 syscall
profile, native MiniAS/MiniLD image linkage, or QEMU host exit-code
capture (the separate offline replay observed guest normal shutdown).
Preserve this exact Image SHA and historical cpio SHA as a runtime regression
baseline; subsequent general compiler changes must re-test the unchanged
oracle before claiming equal or greater coverage.

The repeated old `_compound_head` fault was eliminated after recompiling
`mm/gup.o` with the current MiniC, consistent with correcting loop-mutable
specialization facts. A separate compiler regression source was checked into
the existing RV64 differential suite. Do not mistake this image-level result
for a proof that every other future combination of MiniC optimizations is
correct.


## Independent second private QEMU replay of nine-MiniC-owner Image

An independent QEMU 8.2.2 guest launch of the **identical, authenticated**
Linux Image `8e2a6b39a2b90107dd1c4093bea88d229806812ffb344cea03d2dd855910e0f5`
and the unmodified Python V024 archive
`88c81f1b9eaa3377fbed3f99f29d34c60491eede6ccd57966a609b16b619a51b`
again completed successfully. The test used the original 145/203 guest
`/init`, the same QEMU 1 GiB/RV64 virt environment and explicit DTB
initrd relocation. This is **not** an additional GitHub CI job, and the
user-owned original historical cpio has not been published to GitHub.

- `MODULE_PASS=145`, 145 unique module IDs, final `nouveau`.
- `FUNCTION_PASS=203`, 203 unique function IDs, final `bridge-links-up`.
- Every one of the 348 PASS markers **exactly matched the original Python
  success log in the same sequence**, not just as a set.
- SHA256 of the sorted module-ID canonicalization:
  `a0bf40902063254aec9203600f7b75c54bbacb5106a14dcaca3d31454e6c3b59`.
- SHA256 of the sorted function-ID canonicalization:
  `c1105105a3280ab0d4bbec09e08f57dccc870318791b0fd14b3e6cad86a04a19`.
- Terminal `M38_FINAL145_V028_END pass=203 fail=0`; guest normal
  `reboot: Power down`.
- QEMU process exited `0` (this second replay captured the host status,
  closing the explicit host-exit-evidence gap of the earlier replay).
- No `Kernel panic`, `Oops`, `BUG`, `MODULE_FAIL`, or `FUNCTION_FAIL`.
- Guest final timestamp approximately 631 seconds; independently measured
  host wall 637 seconds.

The previously confirmed Netfilter #111 and GUP/AF_ALG #187 crashes do not
recur in the nine-owner MiniC-only Image. Full-current-object recompilation,
all-new toolchain linking and P1's separate syscall profile remain **outside**
this narrow authenticated Image certificate.
