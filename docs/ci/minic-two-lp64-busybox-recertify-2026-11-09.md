# MiniC AArch64 and LoongArch64 original BusyBox Stage5 recertification request (2026-11-09)

This one-shot exact-head request intentionally reuses the existing PR workflows for both LP64 targets. The request commit changes **documentation only**: no compiler frontend, Core IR, DataLayout, ABI, ISA, Machine IR, or output behavior is modified.

- Trigger `[aarch-stage5-581]`: `.github/workflows/minic-aarch64-busybox-581-stage5-strict.yml`, actual job `aarch64-original-581-mini-c-gnu-qemu`.
- Trigger `[loong-stage5-581]`: `.github/workflows/minic-loongarch64-busybox-581-stage5-strict.yml`, actual job `loongarch64-original-581-mini-c-gnu-qemu`.
- Each acceptance requires the pinned original BusyBox 1.36.1 allyesconfig, 581/581 C owners compiled by exact-head MiniC, zero GNU C fallback, GNU AS/LD, and 38/38 QEMU byte-exact fixtures against GNU. A successful eligibility or smoke step is **not** the full receipt.
- If a job fails, preserve the first actual failing source owner, Kbuild command, ABI/ISA, GNU differential, runtime output and artifact identity. Do not infer success from previous 2026-10-23 receipts or workflow-level green.
- Full Linux Image/P1 for AArch64 and LoongArch64 and all-native MiniPP/MiniAS/MiniLD remain separate milestones; no claim here until actual results are attached.

Related current-source Linux baselines: RV64 2064/2064 and identical-Image P1 12/12 documented in `minic-compiler-throughput-2026-11-09.md`; RV32 1741/1741, GNU-linked ELF32 and identical-Image P1 12/12 documented in `minic-rv32-current-linux-2026-11-09.md`.

## Current-head CI acceptance — independently observed

The original exact-head request on commit `dff443e7aa3c267f04a4d52eb46dd6d8cac1f9fa` completed **both full real-software jobs successfully** on 2026-11-09 UTC; this is more than workflow eligibility or a synthetic smoke test.

| Target | Original Kbuild MiniC C owners | GNU C fallback | GNU AS/LD | Full BusyBox QEMU byte-exact | Saved owner SHA |
| --- | ---: | ---: | --- | ---: | ---: |
| AArch64 | 581/581 | 0 | PASS | 38/38 | 581 |
| LoongArch64 | 581/581 | 0 | PASS | 38/38 | 581 |

- AArch64 real job `129053455089`, [run 42585247529](https://github.com/yituanxing/minic-toolchain/actions/runs/42585247529); emitted `AARCH64_STAGE5_581=PASS`, `FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=PASS`, `failed_owners=0`.
  - Artifact `13659090930`: `minic-aarch64-stage5-581-42585247529-1`, size 62049 bytes, SHA-256 `1c606474597686615bdd5275e612fcffdeecbddea2849cde8bfb711887baacb9`.
- LoongArch64 real job `129053454322`, [run 42585247704](https://github.com/yituanxing/minic-toolchain/actions/runs/42585247704); emitted `LOONGARCH64_STAGE5_581=PASS`, `FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=PASS`, `failed_owners=0`.
  - Artifact `13659295828`: `minic-loongarch64-stage5-581-42585247704-1`, size 62224 bytes, SHA-256 `1d992f537db8851d1ebd17ef45a9c2b043d17d863740568c0a9dfac9ac5f34cb`.

**Scope limit:** the complete original BusyBox C owner corpus passed with GNU AS/LD; this is **not** MiniPP/MiniAS/MiniLD-native certification or a full Linux kernel Image/P1 boot for these two targets.

## Original Linux 6.6.143 re-certification on the updated compiler (2026-11-09)

The historical **three-target** Linux candidate run [#42475998872](https://github.com/yituanxing/minic-toolchain/actions/runs/42475998872) had already passed **all 21/21 producer shards, all three GNU LD/QEMU receivers, and the strict three-ISA result** at compiler code commit `e652462` on 2026-11-08. Old AArch64 GOT/PLT and LoongArch64 vDSO faults are resolved historical issues, not present blockers.

After later compiler throughput and RISC-V emission refactoring, independent **new** one-target Linux campaigns repeated their full guest evidence on unchanged target/compiler source:

| Target | Exact source trigger commit | Original Linux MiniC C owners | Fresh shards | GNU LD | MiniC-linked QEMU P0/P1 |
| --- | --- | ---: | ---: | --- | --- |
| AArch64 | `fe82b11fec74f1c24cbe45e4537a5fbe06dcf914` | 4108/4108 | 7/7 | PASS | PASS/PASS |
| LoongArch64 | `401e63d610e619552605c6b7c89eff24791acc83` | 2723/2723 | 7/7 | PASS | PASS/PASS |

- AArch64: [run #42590416939](https://github.com/yituanxing/minic-toolchain/actions/runs/42590416939), receiver job `129072513246`, `THREE_ISA_SEVEN_RECEIVER=MINIC_ALL_C_GNU_LINK_QEMU_RUNTIME_PASS target=aarch64 owners=4108`, `MINIC_LINUX_RUNTIME_DIFF=PASS` both `p0` and `p1`, vDSO diagnostic errors `0`. Artifact `13661278029`, SHA-256 `d2606e80135b2fcaed11941d9bc3208f5c9efd2dcbe4710a4c05d0b2db44fb02`.
- LoongArch64: [run #42590466619](https://github.com/yituanxing/minic-toolchain/actions/runs/42590466619), receiver job `129071838626`, `THREE_ISA_SEVEN_RECEIVER=MINIC_ALL_C_GNU_LINK_QEMU_RUNTIME_PASS target=loongarch64 owners=2723`, `MINIC_LINUX_RUNTIME_DIFF=PASS` both `p0` and `p1`, vDSO diagnostic errors `0`. Artifact `13662590251`, SHA-256 `2c68a0d68a96c68d7c8dc184e078abb7dc9bea67b9e723c4c4badeb72c2bede9`.

Both one-target workflow conclusions are intentionally `failure` **only** at the strict three-ISA aggregate gate because their other two targets were intentionally not run. Each actual receiver job concluded `success`; a single-target run must not be mistaken for joint three-ISA coverage.

**Toolchain boundary remains GNU assembler/linker**. These Linux guest proofs do not certify all-native MiniPP/MiniAS/MiniLD.
