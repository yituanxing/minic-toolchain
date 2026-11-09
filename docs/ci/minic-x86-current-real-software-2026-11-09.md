# MiniC current-source x86-64 real software and original BusyBox acceptance — 2026-11-09

This is a real GitHub Actions job-log receipt, not a green workflow/eligibility inference. Source commit `a2b6b72980482b679ac811a966c094bd7a049580` on PR #674 includes the scoped x86 BusyBox PIE-compatible MiniC shadow flag, Lua SHA-pinned fallback download policy, and a RISC-V immutable-preflight emission optimization.

- Full run: https://github.com/yituanxing/minic-toolchain/actions/runs/42586578082
- Real six-project job ID: `129057794791`, conclusion `success`.
- Real original BusyBox full Kbuild job ID: `129057794967`, conclusion `success`.

## Six original upstream software projects

`MINIC_X86_SOFTWARE_ALL=PASS projects=6 original_sources=1 GNU_differential=1`:
- tinyAES: PASS
- cJSON: PASS
- zlib: PASS
- Lua 5.5.0: PASS, SHA-256 verified official tarball `57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d`; 33/33 C owners compile and assemble; GNU-linked MiniC interpreter byte-exact GCC output; PIE, native luac and GCC-identical bytecode.
- SQLite: PASS
- TinyCC: PASS, original upstream tests2 suite 36/36.

## Original BusyBox 1.36.1 full Kbuild

`BUSYBOX_X86_ALL_C_OWNERS=PASS Kbuild_linked=593/593 source_TUs=594/594 GNU_C_fallback=0 SHA256_verified=593 runtime_diff=11/11 GNU_AS_LD=1`.

The 593 C objects selected by the full original BusyBox Kbuild were produced by MiniC (no GNU C fallback), GNU assembled/linked, and 11 fixture results compared byte-for-byte against the GNU reference. Initial run #42585588411 had compiled 594 MiniC C units but the final GNU PIE link failed on interposable `link@@GLIBC_2.2.5` due to missing `-fPIE` on MiniC shadow objects. The same strict gate passed after enabling the **already supported** `BUSYBOX_ALLYES_MINIC_PIC=1` in the Makefile. No BusyBox source modifications or acceptance relaxation.

## Scope and remaining milestones

This proves original C compiler + GNU AS/LD real software success, not a fully native MiniPP/MiniAS/MiniLD toolchain and not an original Linux x86-64 Image/QEMU P0/P1 certification. A separate one-target, seven-shard original Linux 6.6.143 current-source candidate replay was requested in commit `ba269b019ac9bc98382d8a598837117d2b5d3f2a`; its outcome must be read independently from https://github.com/yituanxing/minic-toolchain/actions/runs/42586675669.

## Original Linux 6.6.143 current-source GNU/QEMU P0/P1 proof

A subsequent exact-head one-target seven-shard replay `ba269b019ac9bc98382d8a598837117d2b5d3f2a` **PASSED x86-64 original Linux** with the same compiler code and only the one-shot candidate marker changed.

- Official run: https://github.com/yituanxing/minic-toolchain/actions/runs/42586675669
- Source-export GNU input: run `42366441631`, all 21 original `.i/.cmd` shards independently SHA verified.
- Current MiniC compiler binary job `129060111604`: success.
- Seven original Linux candidate shards, each `394/394` MiniC C / GNU AS, zero GNU C fallback; total `2758/2758` fresh MiniC C owners.
- Real GNU Kbuild/LD/QEMU receiver job `129061414584`: success; its log contains `THREE_ISA_SEVEN_RECEIVER=MINIC_ALL_C_GNU_LINK_QEMU_RUNTIME_PASS target=x86_64 owners=2758`.
- Original GNU P0/P1 reference: `THREE_ISA_GNU_QEMU=PASS target=x86_64 lanes=2/2`.
- MiniC-linked real QEMU: `MINIC_LINUX_RUNTIME_DIFF=PASS target=x86_64 profile=p0 GNU_AND_MINIC_ACTUALLY_BOOTED=1` and the same for `profile=p1`.
- Receiver artifact `13660481610`: `minic-linux-seven-receiver-x86_64`, SHA-256 `199b3c23a0e317c2abe933fefac5e7b4dbbf0b4ce1585e1f5d5ba815dfa0529e`, expiration 2026-11-23 UTC.
- Seven candidate object artifacts and exact compiler artifact are associated with the same run; verify them before reuse (retention through 2026-11-16 UTC).

**Important summary caveat:** workflow conclusion `failure` comes exclusively from deliberate `strict-three-isa-result` gate `129065164146`: `MINIC_LINUX_SEVEN_RUNTIME=PARTIAL_CURRENT_MINIC_NOT_THREE_ISA target=x86_64` and `MINIC_LINUX_ONE_TARGET=PASS_7_FRESH_SHARDS_GNU_LD_QEMU target=x86_64`. This requested one-target campaign passes x86 but cannot certify the other two targets. Do not misclassify the x86 receiver's genuine PASS as a Linux failure or infer three-ISA PASS from it.
