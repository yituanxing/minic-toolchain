# MiniC Linux 6.6.143 — reproducible timing ledger (2026-10-10)

Purpose: retain **real CI observations** for post-runtime compiler optimization. Do not compare wall time, summed job durations and compiler CPU work as though they were the same metric.

## Identity / frozen environment

- Runtime branch: `agent/linux-expanded-kbuild-v0`.
- Kernel: Linux 6.6.143, RISC-V 64-bit.
- Pure-GNU golden configuration SHA256: `eeb04f304ebfb97a1079c187dc0c2ea3cc61fc50a55aa3cc776237c16ab19bbe`.
- Exact candidate universe: 2,064 GCC-C translation-unit objects divided into 7 fixed shards (six × 295, one × 294); non-C and post-link derived ELF objects are separately audited.
- GNU GCC preprocesses; MiniC emits RISC-V assembly; GNU assembler/linker generates ELF/Image. Current runtime candidate profile is **not** the separate opt-in performance profile.
- Candidate object cache is addressed by compiler executable SHA256, GCC/assembler identity, config/Image/golden identity, and exact target list. A cache hit is not a new compile benchmark.

## Real seven-runner MiniC compilation

Source: [Actions #38030853231](https://github.com/yituanxing/minic-toolchain/actions/runs/38030853231), commit `92a313bd88b8e9d95a28a43494e096c88933b3ef`. All seven shards successfully compiled and authenticated, then merged all 2,064 object files.

| Shard | Objects | COHORT_FULL_COMPILE elapsed_ms |
| --- | ---: | ---: |
| 0 | 295 | 540849 |
| 1 | 295 | 465861 |
| 2 | 295 | 528877 |
| 3 | 295 | 358678 |
| 4 | 295 | 438748 |
| 5 | 295 | 537343 |
| 6 | 294 | 365296 |
| **Sum (not CPU time)** | **2064** | **3,235,652 ms = 53.93 min** |
| **Slowest shard, approx. parallel critical path** | | **540,849 ms = 9.01 min** |

This excludes runner provisioning, fixture restoration, compiler bootstrap, checks, artifact upload, final GNU relink and QEMU. Kbuild can rebuild some explicitly requested goals, so 2,064 unique outputs does not necessarily mean 2,064 compiler invocations.

The historical ~30-minute Python compiler full-build claim was recalled from an earlier experiment; it is **not a validated matched A/B** on identical Linux input, GNU tools, runner load or build mode. Do not infer a CPU-level speed ratio from it.

## Runtime relink / header-reconciliation observations

| Actions run / compiler commit | Stage | Wall time | Result |
| --- | --- | ---: | --- |
| [#38032780370](https://github.com/yituanxing/minic-toolchain/actions/runs/38032780370), `16ce106` | First GNU Image relink | 46.789 s | GNU Image built |
| same | Kbuild recompile 4 vDSO-offset-dependent MiniC C owners | 15.890 s | 4/4 route SHA verified; second link changed generated headers again |
| [#38033243388](https://github.com/yituanxing/minic-toolchain/actions/runs/38033243388), `cf358022` | First GNU Image relink | 39.797 s | GNU Image built |
| same | Direct GCC-E / MiniC-S / GNU-AS replay of 4 dependent C owners | 9.245 s | 4/4 verified, current vDSO generated header stable |
| same | Second GNU Image relink | 37.501 s | Generated header stable, stopped at derived-object contamination audit (`.pi.o`), before QEMU |

Interpretation: recompiling only the four affected C owners against the newly generated `include/generated/vdso-offsets.h` is both necessary and much cheaper than rebuilding 2,064 C inputs. The full candidate cache remains immutable.

## Telemetry contract for next trials

- `COHORT_TIMING trial=<trial> stage=gnu_first_link elapsed_ms=...`
- `COHORT_TIMING trial=<trial> stage=minic_header_reconcile elapsed_ms=... owners=...`
- `COHORT_TIMING trial=<trial> stage=gnu_reconciled_link elapsed_ms=...`
- `COHORT_TIMING trial=<trial> stage=qemu_<profile> elapsed_ms=... rc=...`
- On actual shard cache **misses**, `COHORT_STAGE_TIME stage=gcc_E|minic|gnu_as ...` records count, summed subprocess wall time, P50, P95, max and slowest input.
- The existing receiver's `cohort.log` and `identity.txt` artifact contain the link/runtime timing measurements. The cache-hit path must say `COHORT_COMPILE=SKIPPED`, not report a fake zero-second compile benchmark.
- Keep build correctness, MiniC provenance, ABI validity, generated-header stability, and real QEMU verdict as separate gates. A successful GNU link alone is **not** a kernel boot pass.

Future matched performance comparison: fix MiniC SHA/config/input list and runner class, collect separate GNU-E, MiniC-S, GNU-as, link and QEMU timings on both baseline and candidate, then optimize the profiled hot path while keeping runtime tests green.

## Confirmed early boot regression and oversized candidate Image

Independent real CI evidence:

- [#38034992904](https://github.com/yituanxing/minic-toolchain/actions/runs/38034992904) measured **MiniC Image 215,502,848 bytes** versus **pure-GCC Image 22,033,920 bytes**, exact same Linux 6.6.143 config and golden build context. **9.781x** larger binary.
- Candidate `vmlinux` allocations: `.text=120,816,528` bytes, `.rodata=85,498,288` bytes; `.init.text=2,996,636` bytes. Not merely padding or debug symbols.
- The 2,064 C-owner intermediate `.o` bytes sum to **462,856,536 MiniC** versus **66,379,160 GCC** (object-file sizes, not allocated section sizes).
- QEMU 8.x RISC-V's pinned initramfs at **0x88200000** overlaps a flat MiniC kernel loaded from **0x80200000** to roughly **0x8cf85000**. This is a host ROM-loader rejection, **not itself a kernel crash**.
- Same MiniC Image booted **without initramfs** for 25 seconds: only OpenSBI firmware output, **no Linux banner**. The **same no-initramfs QEMU command** with golden GNU Image **did reach `Linux version 6.6.143`**, so there is a separate early-boot regression/suspected very slow or stalled guest path beyond the initramfs-address problem. A 25-second no-banner timeout is not a unique-object fault proof.
- The full GNU/MiniC ABI and 40 known GNU-derived ELF transforms remained authenticated; the failure frontier moved beyond original vDSO header changes and object provenance to **real QEMU early-execution diagnosis**.
- `#38035372863` top symbol diagnostics show largest text symbol `sock_ops_convert_ctx_access` **1,101,458 bytes**, `hidinput_configure_usage` **359,082 bytes**, `___bpf_prog_run` **288,374 bytes**. Largest object-file deltas: `kernel/bpf/verifier.o` **+4,248,248 bytes**, `net/core/filter.o` **+3,853,768**, `net/core/dev.o` **+2,216,976**, and `lib/zstd/compress/zstd_lazy.o` **+2,176,016**. These are optimization hot spots, **not proven early-boot culprits**.

**Interpretation and priority:** first resolve the MiniC early boot PC/frontier using unchanged candidate bytes and independent GNU comparison. Meanwhile preserve the precise per-stage timing and object/section growth metrics; C-codegen amplification is a real separate issue worth tackling once first-fault isolation identifies the relevant owners. Never mask this by making QEMU ignore overlapping ROM ranges.

## First real RISC-V guest instruction frontier (QMP, same Image)

Actions [#38036149306](https://github.com/yituanxing/minic-toolchain/actions/runs/38036149306) proved a reproducible diagnostic after removing ONLY the overlapping initrd from the emulator command: RISC-V guest MMU is active (`satp=a000000000087a02`), and guest PC changes inside high-kernel addresses. The original QEMU initrd/P1 oracle still correctly fails closed; **no full kernel boot PASS is claimed**.

Actions [#38036511646](https://github.com/yituanxing/minic-toolchain/actions/runs/38036511646) resolved three QMP snapshots against the *exact same trial's MiniC-linked `vmlinux`*, not GCC symbol offsets:

| Guest sample | Kernel PC | Closest MiniC vmlinux code symbol | Trap CSR evidence |
| --- | --- | --- | --- |
| 2 seconds | `ffffffff87305260` | `strlen+0x0` | `mepc=ffffffff80073da6` → `sbi_ecall+0x2e2` |
| 8 seconds | `ffffffff8759d430` | `setup_earlycon+0x2f2` | same `mepc`, `sepc=80201048` |
| 18 seconds | `ffffffff873052e0` | `strncmp+0x0` | same CSRs |

The PC **is not stable**, and the 25-second previous kernel-only trial emitted only an OpenSBI *serial* banner. These two facts must not be conflated: the guest is executing Linux kernel code, but there is **no Linux version/banner yet**. Golden GCC Image under the identical kernel-only QEMU command does print a Linux banner. This demonstrates a material pre-banner runtime regression or drastic early-path slowdown, but neither a single faulty function nor the exact cause is proven by these three samples. The highest-value next experiment is a **GCC substitution/paired replay of early boot and string/earlycon C owners**, authenticated against the same GNU golden fixture; avoid changing all MiniC compiler optimizations at once.

The reusable QMP collector `tools/ci/linux-runtime-qmp-pc-sampler-v1.py` is read-only and collects registers using a short per-process Unix-domain QMP socket (to respect Linux AF_UNIX path length limits). It does not modify the emulator, kernel, objects or caches. The collector's smoke test and full M0 structure gate passed on the implementation SHA. QMP HMP output is a diagnostic, not a boot oracle.
