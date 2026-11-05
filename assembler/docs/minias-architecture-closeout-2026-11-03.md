# MiniAS architecture closeout (2026-11-03)

**Status: architecture-refactor scope closed; future development is driven by
real assembly, linking, and runtime requirements.** This is a scope and
evidence record, **not** a claim of complete GNU GAS compatibility or a new
four-architecture Linux runtime acceptance run.

Development remains on the single branch
`refactor/minias-target-isa-abi-core` (Draft PR #667).
The production-code baseline reviewed for this decision is remote commit
[`6f6c555`](https://github.com/yituanxing/minic-toolchain/commit/6f6c555b8052290871ec197800c2322899cf4274).
This closeout is documentation-only; its own later commit SHA must not be
mistaken for the SHA of any previous test run.

## Frozen data flow and ownership

| Stage | Canonical owner | Contract to retain |
| --- | --- | --- |
| Configuration/driver | `invocation.c`, `target_registry.c`, `assembler.c` | Immutable `TargetSpec`/DataLayout; one invocation pipeline, no Common architecture switch |
| Source | `source_graph.c`, `source_expansion.c` | Physical source, macros/conditionals, stable source sites and bounded semantic replay |
| Semantics | `common_lowering.c`, `common_context.c` | Typed `MCInst`/expressions/Fragments; one SectionID/SymbolID owner |
| Layout | `mc_layout.c` | One subsection/anchor fixed point for alignment, `.org`, variable data and target branch growth |
| Emission | `common_emission.c`, `mc_encode_result.c` | Transactional final bytes and semantic Fragment/section-owned Fixups; exact measured-size checks |
| Target | RISC-V, x86-64, AArch64, LoongArch64 backends | ISA/ABI, instruction forms and machine-specific fixup policy; no own Section/Fragment writer |
| Debug | `cfi.c`, `cfi_writer.c`, `debug_line.c` | Anchored CFI/line events and already-supported DWARF serialization, after final layout |
| Object | `object_model.c`, `object_writer.c`, `elf_writer.c`, `coff_writer.c`, `object_output.c` | One read-only semantic ObjectModel; ELF/COFF-specific wire encoding; shared atomic publication |

Core Section, Symbol, Fragment, Fixup and layout ownership is no longer an
open-ended restructuring project. In particular, do **not** remove semantic
Source Replay solely because it reparses: layout-sensitive `.if`/`.set`
requires this correctness fallback. Do not merge target-specific machine
encoding or ELF/COFF relocation serializers under a superficial shared name.

## Supported scope and deliberately deferred features

- **In scope now:** the four ELF backend families (RISC-V RV32/RV64,
  x86-64 including i386 profile, AArch64, LoongArch64), source/MC/layout/
  relocation pipeline, existing CFI/DWARF subset, and the validated
  **x86-64 AMD64 COFF** object/link/Windows execution slice.
- **Deferred DWARF:** standalone hand-authored `.file`/`.loc` source can
  produce valid `.debug_line` rows without the synthetic `.debug_info`/
  `.debug_abbrev` CU that GNU GAS emits, so bare-object `addr2line` may
  still show `??:?`. Real GCC `-g` inputs supply their own CU and passed
  separate GNU/MiniAS end-to-end comparisons. This is a feature gap, not
  permission to replace the existing line-table writer.
- **Deferred Windows:** COFF `.cfi_*` intentionally fails closed because
  Win64 SEH/`.pdata`/`.xdata` and CodeView are not implemented. Native PE
  execution success is not Windows unwinding/debugger support.
- **Deferred formats:** Mach-O remains unsupported, with explicit rejection.
- **Deferred RISC-V policy:** GNU GAS normally enables linker relaxation
  by default; MiniAS's explicit policy differs. Do not silently flip the
  default without considering RELAX/ALIGN relocation, CFI, DWARF and LEB.
- **Deferred optimization:** repeated actual-Fixup relaxation-range scans
  may be expensive on large debug-heavy inputs. This is a potential
  performance issue, not a measured correctness failure.

These deliberate gaps are a bounded backlog, **not** grounds for continuing
architecture refactoring indefinitely. Additional instruction/format features
must extend the established owners rather than add parallel data models.

## Acceptance evidence: never conflate SHAs or gate types

### Reviewed production baseline `6f6c555`

| Gate | Result at baseline | Evidence |
| --- | --- | --- |
| GNU assembler oracle and real GCC `-g` DWARF v4/v5 | **PASS** | [run 41600927448](https://github.com/yituanxing/minic-toolchain/actions/runs/41600927448) |
| Target ISA/ABI, RV relocations/CFI, AArch64 GNU/link | **PASS** | [run 41600927455](https://github.com/yituanxing/minic-toolchain/actions/runs/41600927455) |
| Native Windows AMD64 COFF MSVC link/run and atomic output/ACL | **PASS** | [run 41600927453](https://github.com/yituanxing/minic-toolchain/actions/runs/41600927453) |
| Real compiler runtime, strict backend, GCC A/B, BusyBox | **PASS** for their respective jobs | [runtime 41600927446](https://github.com/yituanxing/minic-toolchain/actions/runs/41600927446), [backend 41600927525](https://github.com/yituanxing/minic-toolchain/actions/runs/41600927525), [A/B 41600919165](https://github.com/yituanxing/minic-toolchain/actions/runs/41600919165), [BusyBox 41600919106](https://github.com/yituanxing/minic-toolchain/actions/runs/41600919106) |
| Four-target runtime ladder | **NOT FULL PASS** | [run 41600919182](https://github.com/yituanxing/minic-toolchain/actions/runs/41600919182): x86 Lua stage failed during **external source download**, while other x86 stages and three other target jobs passed; not identified as assembler miscompile |
| Original four-architecture Linux GNU Kbuild/system-QEMU P0/P1; frozen 3x7 and 3536 | **NOT TESTED at this SHA** | Opt-in heavy workflows skipped; older successful SHA below is only historical regression evidence |

### Historical full Linux acceptance (different commit)

At [`5381dc63`](https://github.com/yituanxing/minic-toolchain/commit/5381dc63ca2334f9235e62ce34946df6f335a2c7),
original Linux 6.6.143 GNU Kbuild `.s` assembled by MiniAS and GNU-linked
system-QEMU P0/P1 passed on all four ELF backend families; each recorded
P0 **2/2** and P1 **12/12**. The same commit passed the 21/21 3x7 frozen
replay and 3536/3536 gate, plus A/B, BusyBox and runtime ladder:
[Linux x86/AArch64/Loong](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062804),
[Linux RV64](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062525),
[3x7](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062725),
[3536](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062602).
**Do not transfer this result to a different commit.** The detailed
source/Kconfig/provenance ledger remains in
[`minias-exact-head-full-acceptance-2026-11-02.md`](minias-exact-head-full-acceptance-2026-11-02.md).

## Maintenance and reopening rule

Leave the architecture frozen unless a reproducible original input shows:

1. incorrect machine bytes, ELF/COFF semantics, relocation, ABI or layout;
2. a GNU link or real runtime failure caused by an accepted MiniAS input;
3. a regression in a previously supported, precisely scoped correctness gate;
4. a memory-safety, concurrency-safety or output-atomicity defect.

For such a case, first reproduce with the smallest original `.s` or
single-object link; inspect the whole owning subsystem; group genuinely
related fixes in **one coherent batch**, then run focused GNU/real execution
checks before any expensive exact-head Linux suite. Record production,
tests, workflows and docs separately (+/- lines), and report exact SHAs,
failures, skipped gates and evidence without promoting historical results.

**Closure decision:** no more speculative ownership redesign, no blanket
GNU/LLVM feature chase, and no forced DWARF/SEH/Mach-O work until product
requirements call for it.
