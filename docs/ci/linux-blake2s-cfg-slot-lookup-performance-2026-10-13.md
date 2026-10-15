# MiniC: pathological blake2s Linux TU compile time (2026-10-13)

## Reproduced full-Linux measurement

Runtime P0/P1-accepted run [#38400728544](https://github.com/yituanxing/minic-toolchain/actions/runs/38400728544), 2064/2064 C owners, exact pinned Linux 6.6.143 Kbuild. All 7 shards PASS; one source dominates wall time:

| Shard | Compiled C objects | Actual Kbuild compile ms | Slowest MiniC -S owner | Slowest -S ms |
| --- | ---: | ---: | --- | ---: |
| 0 | 295 | 471744 | net/core/sock.i | 77363 |
| 1 | 295 | **2070730** | **lib/crypto/blake2s-generic.i** | **1981990** |
| 2 | 295 | 543762 | net/packet/af_packet.i | 67430 |
| 3 | 295 | 501456 | net/ipv6/af_inet6.i | 44401 |
| 4 | 295 | 701345 | net/ipv4/udp.i | 84232 |
| 5 | 295 | 490403 | net/core/skbuff.i | 69502 |
| 6 | 294 | 525052 | net/ipv6/ip6_output.i | 76790 |

The slow input was only 148390 bytes preprocessed. It took **1981990 ms (~33m02s) in MiniC -S itself**; GCC preprocessing and GNU assembly are not responsible. The true 7-way compile wall critical path is shard 1, not the summed shard times.

## Root cause in actual compiler logic

`tools/ci/apply-riscv64-p1-defined-before-use-crossblock-v1.py` generates `core_scalar_object_first_access_defines()` inside `src/target/riscv64/core_codegen.c`. For **each candidate CoreObject**, the previous implementation scanned every instruction in the object's owner block. On **every LOAD/STORE** it again scanned **every instruction in the block** looking for the matching OBJECT_ADDRESS producer. In a long basic block or expanded cryptographic function this becomes O(objects × memory operations × block length), needlessly expensive. The existing scalar interval analysis and safety restrictions remain unchanged.

## Targeted implementation change

Replace nested producer search with an array keyed by the dense Core IR `MinicCoreValueId`:

1. One pass collects the first matching `OBJECT_ADDRESS` result's 1-based producer position for the current scalar object.
2. One pass finds the first matching direct LOAD/STORE or parameter object and applies the **same** write-before-read test, preserving the first-producer position and relative order. A forward reference is not misclassified as first-write.
3. Invalid instruction IDs or allocation failure fail closed (disable cross-block reuse); parameter-object handling and all CFG/no-escape safeguards remain in place.

Each candidate's first-access proof changes from O(block_length × memory_ops) to O(value_count + block_length), with an explicit scratch memory bound O(value_count). No change to Linux code, Kbuild flags, 16KiB stack config, or toolchain component selection.

## Acceptance requirements

The original passing 2064-object full-universe cache and image remain immutable under the old compiler identity. The new producer must pass all 7 real Linux shards, ELF/ABI/object identity, GNU link and QEMU P0/P1 in the **existing** workflow; the per-stage profiler must report the new exact `lib/crypto/blake2s-generic.minic-stage2.i` time. Confirm absolute/relative throughput against the original 1981990ms; do **not** claim improvement until measured.

A successful full kernel P0/P1 proves the build path on the tested runtime profile; it does not establish universal correctness or certify unrelated extensions.

## 2026-10-13: cached MiniC native tool union performance frontier

- CI #38415409019 HIT the exact SHA-authenticated 2064 MiniC C objects and pinned GNU Kbuild snapshot, and skipped all seven C producer shards. Therefore no Linux-wide C recompile was responsible for elapsed time.
- Native Kbuild first full link: **310798ms (5m10.8s)**; Kbuild-generated vDSO header changed, exactly four MiniC C owners were directly recompiled in 9250ms, then a second required native full link: **306902ms (5m06.9s)**. Total two native link passes alone are 617700ms (~10m18s).
- These link duration labels in the existing cohort script still say `gnu_first_link`/`gnu_reconciled_link` even though the selected native wrapper actually calls MiniAR and final-three MiniLD links. Do not misattribute the 10m18s to GNU LD.
- Two links reached the final ELF/Image stage. Next strict provenance guard rejected `arch/riscv/kernel/compat_vdso/compat_vdso.o`: native MiniObjcopy legitimately transforms the compat-vDSO DSO; the existing guard only recognized regular vdso.o. Exact CI artifact shows GNU-LD -> native MiniObjcopy `-G` and `-S` -> GNU AS. The new specific authorization is gated on this exact invocation trail plus RISC-V ET_REL/ABI checks; unrelated GNU-only or nonselected mismatches remain forbidden.
- New `minild-times.tsv` times every real MiniLD final pass independently, so the next failure or PASS reveals if five-minute native links are mostly MiniLD CPU time, as suspected, rather than guesses. Native source-built 2064 C objects remain immutable.
- Subsequent cost reduction: pre-stabilize native vDSO generated header and recompile its four precise dependent C owners *before* final full link, to avoid two whole-kernel MiniLD passes. Only implement after full correctness is re-proven and exact header provenance checks preserved.
