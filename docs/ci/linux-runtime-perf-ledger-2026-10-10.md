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

## Two authenticated single-GCC-owner substitutions isolate early console

Real [#38037255841](https://github.com/yituanxing/minic-toolchain/actions/runs/38037255841), compile commit `903c778`:

- All 2,064 MiniC C candidates remained immutable and authenticated; only individual GCC reference object(s) were restored into independent, pristine same-config Kbuild snapshots. Four newly generated `vdso-offsets.h` dependents were separately refreshed with MiniC and SHA-checked, on each trial.
- **All MiniC** (kernel-only QEMU, no initrd): 2s, 8s, 18s guest PCs remained inside `setup_earlycon` (offsets +0x2f2, +0x3fc, +0x3da), without a serial Linux-version banner.
- **GCC only `lib/string.o`**, other 2,063 C objects MiniC: still sampled `setup_earlycon` at 2s/8s/18s (offsets +0x2f2, +0x3fc, +0x2f2). This swap did not improve the pre-banner execution frontier.
- **GCC only `drivers/tty/serial/earlycon.o`**, other 2,063 C objects MiniC: at 2s inside `__minic_inline_spec_1049_10065`, at 8s `__list_add_valid_or_report`, and at 18s `pcpu_chunk_map_bits`. `sepc` also progressed into high-kernel addresses. This is compelling **object-level evidence that MiniC's earlycon.o strongly affects the stall**, but no claim of a fully working serial console or PID1 handoff.
- All three Image sizes were 215,502,848 bytes, ~9.781x GCC. The standard 512 MiB QEMU/initrd loader still refused overlap; only the separate kernel-only QMP diagnostic could measure execution. Each trial remained explicitly **INCONCLUSIVE** against the original exact boot oracle.
- Each repeated experiment avoided recompiling 2,064 C objects, but still incurred ~40s first GNU link, ~9–10s to reconcile four vDSO-dependent C objects, and ~38s second GNU link. This is timing evidence for future reuse improvements.

We also inspected the certified `earlycon.o` RISC-V relocation-aware disassembly. The `setup_earlycon` expression `match < __earlycon_table_end` uses an unsigned pointer comparison; the GCC-linked external `__earlycon_table_end` address is combined with a **zero** index and the correct `sizeof(struct earlycon_id)=152` scale. The nearby multiplication instruction is **not itself evidence of a pointer-scaling bug** (the index register is zero). The increment `match++` legitimately adds 152 bytes. Do not fix that correct pointer-offset code based only on a disassembly grep. The next experiments instrument linker table bounds / live pointer values and test a separate 1024 MiB loader configuration without relaxing the canonical 512 MiB oracle.

These outcomes are causal **diagnostics** for a specific component, not a single-defect proof. GCC `earlycon.o` may avoid one issue while later MiniC defects remain.

## Earlycon source/ELF control-flow discrepancy (2026-10-10, follow-up audit)

- Pinned upstream source: Linux `v6.6.143/drivers/tty/serial/earlycon.c`, `setup_earlycon`. After iterating `__earlycon_table`, the source contains a runtime-conditional second pass: `if (empty_compatible) { empty_compatible=false; goto again; } return -ENOENT;`.
- The *actual certified* MiniC `drivers/tty/serial/earlycon.o` (GitHub Actions shard artifact `11664224186` from the preceding certified run) was independently extracted and disassembled with LLVM objdump, preserving RISC-V relocations.
- In this object, `setup_earlycon` is `.init.text + 0x206e`, size `0x768`. At `0x226c` the pointer-exhaustion loop compares `match < __earlycon_table_end`; its false exit goes directly via `0x22f2` to `.Lsetup_earlycon_core_bb10` at `0x24a2`. That block unconditionally writes false into the `empty_compatible` local and jumps back to the list-entry initialization at `0x21f6`.
- This path contains **no runtime test of the old `empty_compatible` value before the unconditional second-pass goto**, and the MiniC function has no `-ENOENT` return path in its emitted instruction inventory (it does emit `-EINVAL` and `-EALREADY` elsewhere). Thus the source-required second-pass exit appears to have been **omitted in lowering/code generation**, explaining the indefinite earlycon scan for a non-matching boot parameter. The exact responsible frontend/Core defect is not yet isolated: do not assert a specific compiler fix.
- The 2/8/18s QMP snapshots repeatedly sampled `setup_earlycon`, `strlen`, `strncmp` while traversing actual `__earlycon_table` addresses. Swapping only **MiniC `drivers/tty/serial/earlycon.o`** for pinned GCC let the kernel progress to page management and scheduler code. The pinned RISC-V kernel implements `strlen` and `strncmp` via **`arch/riscv/lib/{strlen,strncmp}.S`**, not MiniC's `lib/string.o`; so the earlier `lib/string.o` swap should **not** be interpreted as a direct comparison of those actual assembly routines.
- Next actionable fix is **small-C compiler correctness regression** for a mutable `bool`/goto-backedge/for loop with a two-pass exit (expect exactly two scans and `-ENOENT`), followed by a targeted MiniC Core-lowering / frontend fix. Only after the isolated test passes should `earlycon.o` be regenerated and the cached 2,063 unrelated MiniC objects reused. In parallel, capture actual QMP `sp+0x10` loop-iterator value and `a0/a1` values to cross-check this static CFG finding.
- This is more specific than attributing the regression to generic `strlen` slowness or pointer offset: the pointer step `+152` matches `sizeof(struct earlycon_id)`, but the **once-only retry condition** is not preserved in the observed candidate CFG.

### CI iteration economics

- Preserve single-owner GCC-swap checks as the primary runtime diagnostic; 2,064-object clean rebuild is **not** necessary per hypothesis.
- Focused QMP sampling changed from `2,8,18s` to `1,4,8s` and pinned GCC early comparison is now run only once per receiver instead of again for each single-object swap. These are diagnostic-wall-time optimizations, **not compiler throughput improvement**, and the canonical full initrd/P1 runtime contract remains unchanged.

## Validated QMP loop variable / faster iteration (Actions #38043910784)

The next real GitHub Actions run compiled no new C owners (seven shard caches still authenticated); the receiver successfully GNU-linked the same candidate configuration and collected read-only guest memory:

| MiniC-only, kernel-only QEMU timestamp | `sp+0x10` (exact `match` local) | Relative to `__earlycon_table` | `sp+0x18` length |
| --- | --- | --- | --- |
| 1 s | `0xffffffff878cefc0` | entry 0 | 5 |
| 4 s | `0xffffffff878cf058` | entry 1 (152 bytes) | 8 |
| 8 s | `0xffffffff878cf6e0` | entry 12 (12 × 152 bytes) | 5 |

All are inside the true `__earlycon_table` range `[0xffffffff878cefc0,0xffffffff878cf940)`, whose length is 16 × 152 bytes. This proves that the actual MiniC kernel is **still scanning earlycon entries across multiple seconds**, long after the finite source loop and one allowed retry should finish. The independent disassembly shows its loop exhaustion path unconditionally restarts the scan without testing `empty_compatible`, with no `-ENOENT` return. **This is now a targeted, independently supported MiniC control-flow miscompile hypothesis, not a string-library diagnosis.**

The GCC-only `drivers/tty/serial/earlycon.o` replacement, reusing the other 2,063 MiniC C candidates, has the CPU in `__minic_inline_spec_676_114` after 1 s, `__minic_inline_spec_586_127` after 4 s, and `pcpu_block_update_hint_alloc` after 8 s. It does not produce a full serial Linux banner or PID1 handoff under the kernel-only probe, so do not claim runtime PASS.

Measured receiver stage wall times for that run: full-all first GNU link **47.172 s**, four generated-header-dependent C replays **9.145 s**, second GNU link **37.823 s**; focused single-GCC-owner first link **39.298 s**, header replay **9.302 s**, second link **37.700 s**. The 1/4/8-second QMP series saves ~10 seconds per experiment over the old 2/8/18-second window, and the pure-GCC early boot control is now reused rather than rerun on the focused experiment. Full original QEMU/initrd remains INCONCLUSIVE due Image/ROM overlap.

**Next highest-leverage activity:** reproduce the missing false edge/negative-ENOENT return using a tiny standalone mutable-`_Bool`, `goto again`, `for` program and isolate exactly which front-end/Core pass removes the false edge; only then edit MiniC's compiler implementation. No increase in workflow count or branch count is necessary.

## Exact preprocessed TU captured, standalone approximations pass (2026-10-10)

The pinned [Actions #38045752224](https://github.com/yituanxing/minic-toolchain/actions/runs/38045752224) receiver **did capture the real TU**, before its expected kernel boot INCONCLUSIVE: `trials/full_all/earlycon-source/earlycon-exact.i` (2,531,469 bytes, SHA256 `bd9dd3c9e390690ebb6d0f943b4246f9b25919b7dd4e539adda779d3c84be122`) and the exact current MiniC assembly `earlycon-minic.s` (761,014 bytes, SHA256 `0f7e7b9b21a580df250826e74f22ac2bf37cea7e3676889079a3574835e196fb`). These are fixed CI artifacts, not an estimated reconstruction.

Source `setup_earlycon()` has `if (empty_compatible) { empty_compatible = false; goto again; } return -2;` after `for (match = __earlycon_table; match < __earlycon_table_end; match++)`. Exact MiniC assembly `.Lsetup_earlycon_core_bb10` stores zero to `empty_compatible` (stack `sp+8`) and **unconditionally** jumps to `.Lsetup_earlycon_core_bb6`, while neither an incoming runtime test of `sp+8` nor a `return -2` appears in this function. This is now verified directly from exact GCC-E / MiniC-S bytes, not inferred from an objdump of a linked Image.

**Important counter-evidence:** deliberately reduced two-pass `_Bool`/goto test and a more realistic 152-byte table/continue/strcmp-style probe **both pass** with the same MiniC profile and real QEMU-RV64 execution, as shown by [Actions #38045275922](https://github.com/yituanxing/minic-toolchain/actions/runs/38045275922) and [Actions #38045489140](https://github.com/yituanxing/minic-toolchain/actions/runs/38045489140). Therefore the defect is **not** a general failure of `if(bool)` or backedges; some additional exact preprocessed statement shape, reachability/cold function attribute, or optimization path is necessary to reproduce. These two tests remain regression guards but are not a failing minimal reproducer.

The prior source-only snapshot uploaded the exact bytes after a full QEMU run because the workflow's capture-only tag was accidentally attached to the other `gcc-single` job rather than the `gcc-shard-verify` receiver; the receiver always ran the regular complete experiment. Fix the tag placement in the existing canonical YAML, not by introducing a new workflow or untrusted cache. The tag should make the **next** receiver execute only exact provenance+capture, returning diagnostic PASS while explicitly not certifying boot.
