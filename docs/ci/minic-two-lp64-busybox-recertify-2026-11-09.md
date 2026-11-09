# MiniC AArch64 and LoongArch64 original BusyBox Stage5 recertification request (2026-11-09)

This one-shot exact-head request intentionally reuses the existing PR workflows for both LP64 targets. The request commit changes **documentation only**: no compiler frontend, Core IR, DataLayout, ABI, ISA, Machine IR, or output behavior is modified.

- Trigger `[aarch-stage5-581]`: `.github/workflows/minic-aarch64-busybox-581-stage5-strict.yml`, actual job `aarch64-original-581-mini-c-gnu-qemu`.
- Trigger `[loong-stage5-581]`: `.github/workflows/minic-loongarch64-busybox-581-stage5-strict.yml`, actual job `loongarch64-original-581-mini-c-gnu-qemu`.
- Each acceptance requires the pinned original BusyBox 1.36.1 allyesconfig, 581/581 C owners compiled by exact-head MiniC, zero GNU C fallback, GNU AS/LD, and 38/38 QEMU byte-exact fixtures against GNU. A successful eligibility or smoke step is **not** the full receipt.
- If a job fails, preserve the first actual failing source owner, Kbuild command, ABI/ISA, GNU differential, runtime output and artifact identity. Do not infer success from previous 2026-10-23 receipts or workflow-level green.
- Full Linux Image/P1 for AArch64 and LoongArch64 and all-native MiniPP/MiniAS/MiniLD remain separate milestones; no claim here until actual results are attached.

Related current-source Linux baselines: RV64 2064/2064 and identical-Image P1 12/12 documented in `minic-compiler-throughput-2026-11-09.md`; RV32 1741/1741, GNU-linked ELF32 and identical-Image P1 12/12 documented in `minic-rv32-current-linux-2026-11-09.md`.
