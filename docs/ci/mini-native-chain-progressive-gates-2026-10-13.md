# Mini toolchain Linux 6.6.143 RV64: progressive native-chain gates (2026-10-13)

## Intent — two deliberately separate producer phases

This is the integration plan AFTER the eight-owner MiniC P1 candidate passes its current 16 KiB stack 24/24 same-Image check. Until that is genuinely green, this plan is not an execution/acceptance claim.

**Phase I (GNU CPP only as target-code producer):**

```text
Linux C / .S source
    -> GNU GCC -E / CPP (frozen exact target flags, predefined macros)
    -> MiniC C compiler -S (for C; no GCC cc1)
    -> MiniAS (all relevant target assembler .s input)
    -> MiniAR (all relevant target archive steps)
    -> MiniLD (eventually every relevant RISC-V target LD boundary)
    -> MiniObjcopy / other actually-invoked Mini binutils
    -> Linux Image -> QEMU P0 + P1
```

Phase I does **not** mean `CC=gcc`, `GCC cc1`, GNU assembler, GNU target linker, or GNU archiver: GNU supplies **only preprocessing** in the target build. Host build tools and a separate read-only GNU reference oracle can remain GNU, but they are not counted as native target producers.

**Phase II (all relevant target producers native):**

```text
Linux C / .S source
    -> MiniPP (whole actual target input inventory)
    -> MiniC -S (C; the same certified compiler profile)
    -> MiniAS -> MiniAR -> MiniLD (all relevant target links)
    -> MiniObjcopy / MiniNM / MiniStrip only where invoked
    -> Linux Image -> QEMU P0 + P1
```

Do not label a run fully native if a wrapper silently uses GNU target ld for *non-final* links, GNU target as for selected inputs, or GNU cc1. Treat missing functionality as an explicit blocker and keep the last proven stage immutable.

## Snapshot and existing proof anchors

- MiniC eight experimental stack-frame owners: `ip6_output.o`, `tcp_ipv6.o`, `tcp_input.o`, `slub.o`, `page_alloc.o`, `dev.o`, `hrtimer.o`, `neighbour.o`. Remaining **2056** of **2064** C objects retain original MiniC hashes, no GCC C substitutes. `__neigh_create` 3024 -> 112 bytes, experimental core-CFG semantics not globally proved.
- **Real 12/12 P1** in [#38377015578](https://github.com/yituanxing/minic-toolchain/actions/runs/38377015578), one identical Image SHA `14a108571d52e8dfea851fe9de1dcafd91c22c2029f4aba0f0f772729754e336`.
- Pending stricter **24/24** [#38380007402](https://github.com/yituanxing/minic-toolchain/actions/runs/38380007402). A 24/24 pass is only a scoped runtime stability certificate, not a proof of full compiler semantics.
- Prior independent **GNU-GCC + MiniAS + MiniAR + MiniObjcopy** P0/P1 in [#38185012989](https://github.com/yituanxing/minic-toolchain/actions/runs/38185012989).
- Prior independent four-native chain with **MiniLD at final 3 kallsyms/vmlinux links only** P0/P1 12/12 in [#38190157006](https://github.com/yituanxing/minic-toolchain/actions/runs/38190157006). Its other target linker boundaries remained GNU.
- Prior four-native + **two actual** GCC-cc1 C objects preprocessed by MiniPP in [#38192265904](https://github.com/yituanxing/minic-toolchain/actions/runs/38192265904), **not** a whole-Linux MiniPP certification.
- MiniPP previously passed **3352/3352** GNU-E exact frozen-input comparison [#38150944470](https://github.com/yituanxing/minic-toolchain/actions/runs/38150944470). Do not confuse frozen exact replay with real whole production input coverage.
- Reuse `tests/external/linux/mini_tools_integrated_relink.sh`, `tests/external/linux/minipp_native_full_relink.sh`, the existing `miniobjcopy-strip-regressions-v1.yml`, cached GNU Kbuild `.cmd` metadata, real QEMU test scripts, and the existing locked MiniC object cohort. **No extra Workflow or branch** for each tool/stage.

## Stage-specific strategy (after the eight-owner gate)

| Gate | Frozen reference | Target producer replacement | Cheapest informative check | Promotion requirement |
| --- | --- | --- | --- | --- |
| G0 | Exact eight-owner MiniC 16 KiB Image + immutable 2064 original candidate manifest | GNU -E -> MiniC; GNU AS/AR/LD/objcopy for *baseline only* | 24 same-Image P1 boots and object SHA/ABI identities | 24/24 plus relevant raw guest markers; capture versioned artifacts |
| G1 | G0 object sources and (where available) frozen genuine MiniC `.s` | Add **MiniAS**, leave GNU AR/LD/objcopy | First 1–8 representative actual MiniC-generated `.s`, then all relevant target `.s`; ELF sections, relocations, image relink | Real P0/P1 from actual MiniAS-produced objects; no silent GNU assembler fallback |
| G2 | G1 same-tree objects | Add **MiniAR** and **MiniObjcopy** (reuse existing historically successful pair) | Archive and Image-only relink; inspect actual AR and objcopy trace/call counts | Real P0/P1 and native production provenance |
| G3a | G2 same-tree archives/objects | **MiniLD at the three historically supported final links** | Linked ELF/map/kallsyms/relocations; targeted incremental link | Real P0/P1; explicitly describe remaining GNU ld boundaries |
| G3b | G3a same-tree | Replace **all remaining target LD boundaries** with MiniLD; include any kernel sublinks/vDSO or intermediate link actions actually invoked | Start at the first unsupported command, smallest ELF oracle and GNU differential, rerun bounded link | Zero target GNU ld production calls; full original P0/P1 |
| G4 | G3b complete native object producer chain | **GNU -E as sole remaining GNU target producer**, MiniC+MiniAS+MiniAR+MiniLD+MiniObjcopy all active | 1–3 normal P1 then 12/24 same-Image boots; compare symbols/sections to reference | P0 2/2, P1 12/12 then 24/24, complete provenance; do not declare entire compiler correct |
| G5 | Pin successful G4 C/assembly input semantics | Replace GNU -E with **MiniPP for all actual target C and preprocessed assembly inputs**; MiniC consumes those `.i` bytes, never GCC cc1 | Exact compare every MiniPP result against frozen GNU `.i` with real command args; if byte-equal, permit *SHA-attested object reuse* instead of pointless recompiles, but record genuine MiniPP producer traces | Fully native target producer boundary proof and P0/P1 plus repeated stable boots; report unsupported preprocessor paths honestly |

**G5 caveat:** A host GCC used to build the tools or a GNU **reference-only** oracle is different from a GNU target-code producer. If MiniPP still invokes GCC to discover predefined target macros or preinclude state during actual build, G5 is **not** completely GNU-independent until these values come from an explicit verified independent profile. Handle generated `.S`, vDSO inputs, and Kbuild dependency semantics explicitly; do not replace “whole actual inputs” by selected sample-only claims.

## Time/cost guardrails

1. **G0 first:** no integration test launches on a 24-boot failure, and a green workflow badge without raw guest evidence is not a pass.
2. Reuse the **same GNU baseline** and certified `.o` SHA pool; don't rebuild all 2064 MiniC C objects for each AS/AR/LD hypothesis.
3. Within one gate, fail fast on one real object/assembly/archive/link boundary; persist the first failing command, command-line flags, input hash, output, GNU oracle and a minimal test.
4. Avoid rebuilding full Linux to change just AR/LD/objcopy. The earlier same-tree script achieved **~10s/15s incremental relinks** in some stages; these are historical measurements, not an unconditional ETA.
5. A distinct producer boundary invalidates the affected downstream outputs (e.g., changing AS requires reassembling its real `.s`, not claiming GNU-as `.o` as native). Capture SHA for all reused vs rebuilt objects, linker provenance, Image SHA, and per-QEMU guest logs.
6. No GNU fallback in a gate's claimed native boundary. Fail closed if it appears, then repair at the single failing owner. Each success becomes an immutable rollback point before adding the next tool.
7. When all non-PP tools are actually native in one Image and pass P0/P1, **only then** pursue full MiniPP production substitution. Self-hosting, interactive shell scope and broad optimizer soundness are later, independent gates.

## Required evidence for every gate

Record source commit and tool binary hashes; exact preprocessor identity; compiler `-S` provenance; the count/identity/ABI hashes of GNU, Mini and cached target objects; all MiniAS/MiniAR/MiniLD/MiniObjcopy command traces; Link ELF/Image SHA and map; actual QEMU guest serial/P0/P1 markers; number of identical-Image boots (and failures); exact remaining GNU target producer boundaries. No blanket “all Mini tools passed” unless this table is fully native for the target build.


## 2026-10-14 actual incremental GNU LD fallback frontier

- Proven mixed Linux 6.6.143 runtime gate #38482154264: SHA-restored **2064 MiniC C objects** (no shard builds), **MiniAR all 640 Kbuild operations**, **MiniLD two vmlinux.o intermediate `-r` links** plus three final static links per Kbuild pass, and **MiniObjcopy**. Fast boot and QEMU P1 12 syscalls PASS. GNU GCC -E, GNU AS, GNU LD for untested purgatory/vDSO still present.
- Native `purgatory.ro` **relocatable** isolated oracle #38482832436: 4 ms, GNU RISC-V ET_REL flags and entire global symbol-name set equal; without any C object rebuild. **Full cached mixed-toolchain QEMU gate #38483088261 PASS**: exactly 2064 MiniC C objects SHA-reused, all 640 MiniAR calls native with GNU AR zero, MiniLD for 1 actual purgatory.ro -r, both vmlinux.o -r, six final MiniLD links (two phases), MiniObjcopy and QEMU fast + full P1 12 syscalls. Genuine full P1 outcome, not only isolated synthetic fixtures.
- `purgatory.chk` **static executable** isolated oracle #38483343366: MiniLD succeeds in ~3 ms and produces correct RISC-V ELF64 ET_EXEC ABI with nonzero entry, **but is NOT GNU equivalent**. GNU exports `__BSS_END__`, `__DATA_BEGIN__`, `__SDATA_BEGIN__`, `__bss_start`, `__global_pointer$`, `_edata`, `_end` not emitted by MiniLD; MiniLD emits `__init_array_*`, `__fini_array_*`, `__preinit_array_*` absent from GNU. The layout also differs: `purgatory_start` GNU 0x1138c vs MiniLD 0x11310, and some function sizes differ (e.g. `__memcpy` GNU 0xf6 vs MiniLD 0xf8), indicating different linker relaxation/placement. Do **not** replace this GNU LD final purgatory boundary or wave it through by weakening symbol comparison. Inspect architecture-specific linker synthesis and RISC-V relaxation semantics in a separate focused oracle first.
- Linux RV32 compatibility vDSO currently passes `-melf32lriscv` to GNU LD; current MiniLD accepts only `elf64lriscv`, so cannot claim that boundary native. RV64 vDSO uses `-shared` and `-T` script, `--hash-style=both`, `--eh-frame-hdr` etc. Current MiniLD shared path does not implement that full production behavior. Track as explicit unresolved GNU fallback, not hidden replacement.
- Actual currently targeted faster method: one GNU-cached linker command per owner with input hashes → differential ELF/ABI/symbol test → only promote proven operation → exact original 2064 MiniC pool and full QEMU P0/P1. No new workflows, no regeneration of all C objects.
