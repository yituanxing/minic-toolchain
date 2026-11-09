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
