# MiniC RV32I original Linux 6.6.143 current-head recertification (2026-11-09)

**Decision: PASS for RV32I/ILP32 Linux P0 and P1 on the current compiler source. This is not a five-ISA or all-native toolchain acceptance.**

- Exact trigger/producer HEAD: [`d2307913ad401561d8015d9fc7d51b1e9a722817`](https://github.com/yituanxing/minic-toolchain/commit/d2307913ad401561d8015d9fc7d51b1e9a722817), `[linux-rv32i-kbuild]` with canonical config SHA-256 `4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c`.
- [Actual push workflow #42583285758](https://github.com/yituanxing/minic-toolchain/actions/runs/42583285758): seven independent `gcc-shards (0..6)` jobs **7/7 SUCCESS**, `gcc-shard-verify` receiver job **SUCCESS** (job ID `129048145927`). This is not an eligibility-only green check.
- Receiver steps 42–43 downloaded and authenticated all seven just-produced candidate shards, checked the **1741/1741** MiniC ELF32 objects and built a fresh original-GNU-linked Image. Step 44 `Boot THIS commit's newly linked 1741-object RV32 Linux in real QEMU` **SUCCESS**; log reports `RV32I_FULL_MINIC_CURRENT_HEAD_QEMU_P0=PASS 1741/1741`.
- Step 45 `Verify current MiniC RV32 Linux shell and P1 12/12 on same Image` **SUCCESS**. The actual log reports `RV32I_CURRENT_HEAD_SHELL=PASS`, twelve independent `RV32I_CURRENT_HEAD_P1_TRIAL=PASS attempt=N/12` lines, each `syscalls=12/12`, and final `RV32I_CURRENT_HEAD_P1=PASS boots=12/12 syscalls=12/12 MiniC_C=1741/1741 original_Kbuild=true ELF32=true`.
- Same original GNU-linked Image SHA-256 for all twelve trials: `89b43f766e9d81566aa2098431a12ec19dbf1c4e7a5c6a05ebc4b2ff5e82664f`.
- Step 46 evidence archival **SUCCESS**. Artifact [`13658735722`](https://github.com/yituanxing/minic-toolchain/actions/runs/42583285758/artifacts/13658735722), `linux-rv32i-seven-shard-Image-d2307913...`, SHA-256 `f1fb0412519a4db3ce5ef81240c05cb19f60b68aae97f5211635fc9e90246993`, uploaded 2026-11-09 08:11:54 UTC (default retention expires 2026-11-16).

## Claim boundary

This proves current-source MiniC C compilation through original Kbuild preprocessing, GNU assembly/link and true QEMU RV32I kernel boot/syscall P1, with a reproducible same-Image identity. It does **not** independently prove MiniPP/MiniAS/MiniLD as a fully native chain, all RV32 P2/P3 scenarios, current Stage3/Stage4 fixed-point self-hosting, or the x86-64/AArch64/LoongArch64 full Linux Image/QEMU gates. Previous RV64 2064/2064 and same-Image 12/12 evidence remains separate in `docs/ci/minic-compiler-throughput-2026-11-09.md`.
