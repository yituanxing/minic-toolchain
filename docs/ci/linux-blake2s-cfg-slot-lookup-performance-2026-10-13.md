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
