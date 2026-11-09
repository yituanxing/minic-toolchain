# MiniC five-ISA original Linux 6.6.143 C compiler acceptance — 2026-11-09

## Verdict

**PASS for MiniC C compiler + GNU assembler/linker + real QEMU Linux P0/P1 on all five ISA targets.** This is not a claim of native MiniPP/MiniAS/MiniLD end-to-end Linux, P2/P3, or parity of generated code quality against GCC.

This cross-run campaign uses the **same compiler source** introduced by commit `a2b6b72980482b679ac811a966c094bd7a049580` (the sealed RISC-V MIR preflight emission path). The 10 subsequent commits through `e5bf93791b0c4fb39b531d833ade0e05ffd792c7` change only docs and dedicated CI trigger marker files; GitHub compare verified no compiler, frontend, Core, ABI, or target code changed. Each real job additionally pins its own exact commit and preserves its actual evidence.

| ISA | Original Linux C owners compiled by MiniC | GNU LD | Real QEMU oracle | Source run | Source commit |
| --- | ---: | --- | --- | --- | --- |
| RV32I | 1741/1741 | PASS | P0/P1, identical Image P1 boots 12/12; each syscalls 12/12 | [42591386538](https://github.com/yituanxing/minic-toolchain/actions/runs/42591386538) | `334e3e59f131a60949c54df26d439d204d99bbce` |
| RV64I | 2064/2064 | PASS | P0/P1, identical Image P1 boots 12/12; each syscalls 12/12 | [42591778229](https://github.com/yituanxing/minic-toolchain/actions/runs/42591778229) | `5cec145d0474d049a0940e86d238a2abd42efc04` |
| x86-64 | 2758/2758 | PASS | P0/P1 | [42586675669](https://github.com/yituanxing/minic-toolchain/actions/runs/42586675669) | `ba269b019ac9bc98382d8a598837117d2b5d3f2a` |
| AArch64 | 4108/4108 | PASS | P0/P1 | [42590416939](https://github.com/yituanxing/minic-toolchain/actions/runs/42590416939) | `fe82b11fec74f1c24cbe45e4537a5fbe06dcf914` |
| LoongArch64 | 2723/2723 | PASS | P0/P1 | [42590466619](https://github.com/yituanxing/minic-toolchain/actions/runs/42590466619) | `401e63d610e619552605c6b7c89eff24791acc83` |

## Authoritative log markers and preserved artifacts

- **RV32I** receiver `129074968494`: `RV32I_CURRENT_HEAD_P1=PASS boots=12/12 syscalls=12/12 MiniC_C=1741/1741 original_Kbuild=true ELF32=true immutable_Image=4c8b0d39e5b950000bd858dc303008327636b2d95384b040f3f46f9baca995c9`. Image artifact `13662125724`, SHA-256 `2962cd8d814f55d27a631bf741a4e447e1b2e12a622368a3581a816d96a78964`.
- **RV64I** receiver `129076532705`: `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true` and `RV64_FULL_IDENTICAL_IMAGE=PASS repeats=12/12 syscalls_each=12/12 image_sha256=63806b69e4341b0ad4bb08489dd666b36c6bd803532680ed065836eb4d368321 head=5cec145d0474d049a0940e86d238a2abd42efc04`. Receiver artifact `13662531987`, SHA-256 `a9cd4bb0422d565008fe512d599fcb64ce07349681cb628e877f18fdca34d95c`.
- **x86-64** receiver `129061414584`: `THREE_ISA_SEVEN_RECEIVER=MINIC_ALL_C_GNU_LINK_QEMU_RUNTIME_PASS target=x86_64 owners=2758`, plus `MINIC_LINUX_RUNTIME_DIFF=PASS` on profiles `p0` and `p1`. Receiver artifact `13660481610`, SHA-256 `199b3c23a0e317c2abe933fefac5e7b4dbbf0b4ce1585e1f5d5ba815dfa0529e`.
- **AArch64** receiver `129072513246`: `THREE_ISA_SEVEN_RECEIVER=MINIC_ALL_C_GNU_LINK_QEMU_RUNTIME_PASS target=aarch64 owners=4108`, P0/P1 both actually booted, vDSO diagnostic errors `0`. Receiver artifact `13661278029`, SHA-256 `d2606e80135b2fcaed11941d9bc3208f5c9efd2dcbe4710a4c05d0b2db44fb02`.
- **LoongArch64** receiver `129071838626`: `THREE_ISA_SEVEN_RECEIVER=MINIC_ALL_C_GNU_LINK_QEMU_RUNTIME_PASS target=loongarch64 owners=2723`, P0/P1 both actually booted, vDSO diagnostic errors `0`. Receiver artifact `13662590251`, SHA-256 `2c68a0d68a96c68d7c8dc184e078abb7dc9bea67b9e723c4c4badeb72c2bede9`.

The three seven-runner one-target workflows (x86-64/AArch64/LoongArch64) intentionally return overall `failure` in the **strict three-ISA aggregate job**, because only one target is requested per run. In each run all seven actual target shards and the actual target GNU LD/QEMU receiver are `success`. A separate original three-target campaign [#42475998872](https://github.com/yituanxing/minic-toolchain/actions/runs/42475998872) also has **21/21 shard success, 3/3 receiver success, and strict aggregate success** on older compiler source commit `e652462` (2026-11-08).

## Scope of compiler performance and future work

The pipeline proves correctness against original software and real Linux guests. The RISC-V prepared-MIR duplicate validation removal passed tinyAES/cJSON assembly SHA identity 10/10 across the five ISAs and MIR negative verifier tests. Local timing was too noisy to claim speedup; stable same-runner compiler A/B run [#42592630648](https://github.com/yituanxing/minic-toolchain/actions/runs/42592630648) is independent and must be inspected before making a throughput claim.

Future gates: all-native MiniPP/MiniAS/MiniLD, Stage4 current-source self-hosting, P2/P3 coverage, and stable end-to-end compiler throughput vs historical best. These are **not** represented as PASS by this document.
