# Linux post-8250 spinlock frontier — 11 October 2026

## Frozen baseline

- Linux 6.6.143 RV64, exact GNU golden source and linker, 2064 authenticated C objects.
- Four current-MiniC repairs: `drivers/tty/serial/earlycon.o`, `fs/super.o`, `net/ipv4/tcp_cong.o`, `net/netlink/genetlink.o`. The other 2060 objects remain frozen. QEMU no longer shows the previously isolated jhash panic or genetlink WARN_ON; P1 remains not certified.

## Repeated post-8250 PC — measured, not assumed

Baseline UART/QMP artifact (run `38110120236` and independently replayed in `38111505840`) has the same PC `0xffffffff804a9ffe` at samples t=12,20,32 s, in the stack vicinity of `do_raw_spin_unlock` / `rpm_resume`; the return address repeats `0xffffffff804a9ff2`. Repetition strongly suggests a hot path, but does not alone prove a stuck instruction or a particular compiler defect.

## Causal single-object GCC swap (run 38111505840)

- Confirmed via `riscv64-linux-gnu-nm --defined-only` that `kernel/locking/spinlock_debug.o` owns `do_raw_spin_unlock`.
- Rebuilt and relinked with **only that C object** taken from the authenticated GCC cache; the four repaired MiniC objects and 2059 other MiniC objects stayed unchanged. GNU link and Kbuild header-identity reconciliation passed. Same QEMU kernel command line and samples t=1,4,8,12,20,32 s.
- GCC-swap PC measurements: t=12 `0xffffffff843c5106`, t=20 `0xffffffff8049b10c`, t=32 `0xffffffff8049b890`. Late samples: baseline 1 unique PC versus GCC swap 3 unique PCs. PC addresses belong to different linked kernels; use diversity and function-symbol traces, **not address equality across kernels**, as the comparison.
- Neither run shows the old Netlink WARN_ON, earlier page fault, or a kernel panic in the sampled logs. The GCC swap also did **not** reach `Run /init as init process` in its bounded window; P1 is **NOT_CERTIFIED**. This is evidence of a changed hot-path frontier, not a demonstrated successful boot or definitive root cause.

## Exact current MiniC replay (run 38112013620)

- Under the current complete patched MiniC profile, replayed only `kernel/locking/spinlock_debug.o` using the same frozen GNU Kbuild preprocessing and assembler setup. Route audit passed.
- Frozen MiniC object SHA256 = newly compiled MiniC object SHA256 = `8163d89a8a3290e2c176d98324927b9581c14bd96689d16b12689b0e16c19b89`. **Existing compiler fixes do not change this object.**
- Captured exact `spinlock_debug.i` (2,123,493 bytes), MiniC `spinlock_debug.s` (752,438 bytes), and three disassemblies (GCC, frozen MiniC, current MiniC) in run `38112013620`, artifact `11691822360`, under `earlycon-hotfix/spinlock-debug-capture/`. **No GNU full kernel relink** in this capture run.
- At the `do_raw_spin_unlock` C interface, GCC mostly inlines unlock logic, while MiniC emits calls to `mmiowb_spin_unlock`, `debug_spin_unlock`, `arch_spin_unlock`. Inlining differences alone are **not** proof of wrong semantics. The MiniC `arch_spin_unlock` retains a `fence rw,w` followed by a halfword release store, but its code is much more indirect and deserves scrutiny for correct address/value flow.

## Next narrow proof

1. Isolate the ticket-lock release instruction in the *same* `arch_spin_unlock` routine and compare the address and halfword value derived by MiniC against GCC/source. If no semantic mismatch, distinguish performance/hot-loop amplification from genuine lock-state corruption, e.g. via constrained GDB/QMP and an independent small lock-state runtime regression.
2. Prefer a reproducible compiler-core semantic defect and a focused regression before changing the compiler again. If identified, rebuild just `spinlock_debug.o`, then test the same five-MiniC-owner mixed kernel using cached GNU objects.
3. Keep CI canonical: no new workflow, no full 2064-TU recompilation and no full/P1 PASS claim until init actually starts.


## Subsequent QMP symbolization and serial-core GCC control

- A/B run `38114168879` (commit `b47a564`) completed. With MiniC `spinlock_debug.o`, samples t=12/20/32 show `pc=do_raw_spin_unlock+0x30`, `ra=do_raw_spin_unlock+0x24`, `sepc=serial_core_register_port+0x3f4`. With only GCC `spinlock_debug.o`, late PCs visit `_raw_spin_lock`, `rpm_resume`, and an inline-specialization function; sampled `sepc=arch_local_irq_restore+0x8a`. Neither candidate reaches `/init`.
- Confirmatory owner swap run `38114602056` (commit `0d6a9cb`) swapped **only** `drivers/tty/serial/serial_core.o` to GCC, leaving `spinlock_debug.o` in MiniC. The verified owner symbol is `serial_core_register_port` (index 1151 of 2064). Baseline late PC remains `do_raw_spin_unlock+0x30` at 12/20/32s. GCC serial-core late PC remains `do_raw_spin_unlock+0x30` at 12 and 20s and is `rpm_resume+0x1b6` at 32s. No `/init`, panic, or previously fixed Netlink WARN.
- SEPC varied across runs even in the nominal unchanged candidate: in this run baseline `kernfs_activate_one+0x206` and GCC serial-core `partial_name_hash+0x12c`. Do not mistake SEPC changing across independent run images for direct semantic proof; kernel layout, interrupts, and runtime timing differ.
- These swaps perturb the late hot path, but neither proves the defect is in `spinlock_debug.o` or `serial_core.o`. MiniC's unlock path contains additional calls and stack operations; no demonstrated wrong halfword release store. Different PCs are **not** an accepted boot-causality certificate.
- Use a narrower next probe: collect dynamic unlock pointer/state and current task/IRQ context (same-image GDB or bounded QMP) and compare with GCC reference. Separate pathological code-generation slowness from corrupted lock state before modifying compiler lowering. Reuse exact immutable object fixtures and existing workflow.
- Runtime wall clock evidence: `38111505840` 5m38s, `38114168879` ~6m42s, `38114602056` ~5m54s. The two GNU-link cycles per A/B (each first and reconciled pass) are major mechanical cost; isolate analysis time from CI wait. No full Linux C-object rebuild required.
