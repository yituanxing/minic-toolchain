# RV32I Linux 6.6.143 runtime fault census (2026-10-25)

Status: **FAIL**. MiniC's RV32I/ILP32 Linux ELF32 Image links with all 1741
attested C objects, but crashes under genuine QEMU boot. No P1 12/12 runtime
acceptance has passed. GitHub Actions diagnostic job success means the fault
isolation runner executed, **not** that a tested Linux kernel passed.

## Frozen controls and historical verification

- Exact original GNU Kconfig SHA256:
  `4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c`.
- Original seven-shard producer Actions run: `40189397298`; 1741/1741
  MiniC ELF32/ILP32 objects and GNU Kbuild linkage, with original object
  owner manifests/SHA attestations. The original full MiniC Image SHA256 was
  `c2e52427e6bc861216d0389e1d408d845b5606e2f67a8e23a805c11d29bfaf58`.
- P0 full Image test: `40191082565`; GNU reference PID1 `getpid`/`write`
  boot succeeds, full MiniC Image crashes before PID1 with scheduler
  stack-end corruption.
- Frozen GNU source/object cache is reused in the differential QEMU script;
  every runtime replacement preserves original Linux Kbuild and `.config`,
  pins the same RV32 software arithmetic library, verifies target object
  SHA256 after Kbuild relink, and must first boot a fresh GNU relink control.
- 7 x 8 disjoint ~31-object subsets: run `40193086148`; 17 P0 PASS,
  39 P0 FAIL. 7/7 original GNU relink controls PASS.
- Nine representative four-object groups were isolated in run
  `40193582510` (47 passing vs 9 failing four-object groups across 56
  subgroups). The nine failures are distributed across seven of the 39
  original failing 31-object subsets, **not** an exhaustive fault count.
- Nine groups reduced to one-object replacements in run `40194269014`,
  repository commit `9f25f588dced265747dd3fc93350e003dc98914d`.
  Across 35 real QEMU single-object trials, 26 P0 PASS and 9 P0 FAIL.
  Every of the nine isolated failures reproduces when *only that MiniC C
  object* replaces its original GNU counterpart; other owners remain GNU.

## Real QEMU single-object offenders

| Original Linux C object | QEMU initial failure when substituted alone |
| --- | --- |
| `mm/gup.o` | `Kernel panic - not syncing: corrupted stack end detected inside scheduler` |
| `fs/exec.o` | Same scheduler stack-end panic |
| `drivers/tty/serial/8250/8250_port.o` | Same scheduler stack-end panic |
| `fs/super.o` | NULL dereference at `00000023` |
| `lib/assoc_array.o` | NULL dereference at `00000013` |
| `drivers/regulator/core.o` | NULL dereference at `00000008` |
| `security/keys/keyring.o` | Paging request at `8c14f2d0` |
| `net/core/rtnetlink.o` | Paging request at `504f8f92` |
| `drivers/base/regmap/regmap.o` | Paging request at `15a9cd11` |

No source-level backend defect or semantic patch has yet been proved by
these runtime trials. Panic categories are observations, not root causes.
Do not say Linux RV32I passed because all nine isolation CI jobs conclude
`success`: the triage script currently intentionally reports job success
after successfully recording actual mixed-kernel failures.

## Required next work and complete acceptance

1. Use these nine *real failing* owner inputs, the original GNU object,
   generated MiniC object, GNU/MiniC assembly and panic call traces to
   identify shared RV32 semantic, stack/ABI, or memory lowering faults.
2. Make a coherent, architecture-reviewed batch correction in the existing
   MiniC Frontend/Core/Target boundaries. Do not patch individual Linux
   source files or modify MiniAS/MiniLD/MiniPP to hide MiniC bugs.
3. The other 32 failing ~31-owner groups have not yet been narrowed to one
   C owner; expand the census or use fixed newly-produced shards to prove
   the bug class has disappeared. Current nine are lower-bound examples.
4. Rebuild all 1741 objects by the original seven-shard producer contract,
   retain manifests and SHA checks, link through unmodified GNU Kbuild, then
   run the exact pinned Linux QEMU P0/PID1 and P1 **12/12** before PASS.

Evidence URLs:

- https://github.com/yituanxing/minic-toolchain/actions/runs/40189397298
- https://github.com/yituanxing/minic-toolchain/actions/runs/40191082565
- https://github.com/yituanxing/minic-toolchain/actions/runs/40193086148
- https://github.com/yituanxing/minic-toolchain/actions/runs/40193582510
- https://github.com/yituanxing/minic-toolchain/actions/runs/40194269014
