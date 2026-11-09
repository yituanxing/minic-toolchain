# MiniC boundary convergence after IR-P24 (2026-11-03)

This is the **actual-source / local-batch** record for the next MiniC-only
architectural cleanup. It is not a Linux runtime certification and does not
authorize changes to MiniPP, MiniAS or MiniLD. The starting PR #674 branch is
`refactor/minic-target-language-boundary`, exact parent `c8a6ad11220a8cbd8cb2f2376ad043869b9ef76f`
(IR-P24). The integrated batch has not yet been assigned a commit SHA here.

## Source inventory and ownership

The older `minic-global-architecture-audit-2026-10-29.md` count of 319
tracked `src/` C/H files is a historical snapshot, **not the denominator for
this tree**. At IR-P24 the tracked `src/**/*.{c,h}` inventory has **379**
files: 71 Frontend, 15 Compiler, 58 Core, 232 Target, and 3 Common.
Introducing `src/target/backend_source.{c,h}` increases that inventory to
**381** once they are tracked; this is a file count, not an assertion that
381 files were deeply reviewed or refactored. The review-ledger's blob-based
`pending` status must likewise not be converted to an unreviewed-file or
runtime-failure count.

The relevant production data flow remains:

1. Frontend owns C/GNU source types, AST, declarations and initializers.
2. Source lowering admits inline ASM and imports source types into a transient
   function draft and the shared CoreType arena.
3. Published CoreFunction and ModuleIR own sealed semantic instructions,
   symbol identities, global initializer fragments and module metadata.
4. Target machine planners consume only the sealed Core/Module plus ISA/ABI
   policy; each target owns its actual register/frame/relocation syntax and
   assembly output. GNU AS/LD and QEMU remain separate acceptance stages.

## Paths removed or made single-owner in this batch

### Source backend versus machine backend

The former `target/backend.h` / `backend.c` mixed two lifetimes: the final
Core+Module machine emitter and source-level GNU inline-ASM qualification,
constraint capture, asm-goto, normalization and clone policy. The source
descriptor and its operations have moved into
`target/backend_source.{h,c}` under `MinicSourceBackend` and
`minic_source_backend_*`. The machine `MinicBackend` now retains the sealed
module emitter and reachability-emission policy; it no longer publishes C
Program or GNU source-operand callbacks. The consumers in Core ingress,
Compiler inline specialization, the three target source-ASM owners and their
standalone probes have been retargeted. Both descriptors resolve via the
**same canonical target registration inventory**, preserving strict unknown-
target rejection without introducing Frontend ISA switches. RV32/RV64 source
policies still retain their earlier qualified fallback behavior; this split
does not broaden GNU ASM acceptance.

This is an **ownership split**, not deletion of the actual source qualifier
algorithms: the source fingerprint, ties, memory effects and exact ISA form
qualification remain active on the source side, while machine emission
continues to verify the sealed target form independently.

### Global initializer ingress versus ModuleIR

`compiler/global_payload.{h,c}` now directly returns
`MinicCoreModulePayloadKind` and `MinicCoreModuleDefaultSection` instead of
maintaining a second, isomorphic 10-way payload enum plus GAS-section text.
The obsolete `module_ir_build.c` 10-case enum conversion and section-string
parser have been removed. Source/DataLayout still performs the actual C
initializer shape and byte-layout walk, then `module_ir_build.c` publishes
owned Module fragments. No C-file fallback global writer has been restored.

The relocation sink now validates and owns the complete symbol/addend before
advancing the fragment cursor. A rejected relocation cannot commit a partial
fragment or leave an unowned symbol pointer in the published Module.

### One fragment/symbol contract, target-only directive restrictions

The target-neutral `minic_core_module_global_fragments_valid` contract now
serves Module verification and the shared global serializer. It checks
contiguous offsets, bounded/nonzero sizes, exact storage coverage, legal
scalar widths, relocation kind, canonical `.Luser_N` label names and symbolic
identity. The machine writer no longer maintains a second equivalent fragment
layout verifier; it keeps **only target/GNU directive capability and relocation
width checks**. The shared conservative ASCII symbol-name predicate in
`core/symbol_identity.h` replaces duplicate lexical scanners in Core/Common
and RV64. This is deliberately a safe subset for emitted names, not a claim
that future COFF/Mach-O writers must accept only that subset.

The same batch makes partially allocated CoreFunction and C ingress-draft
destruction safe when an allocator fails between count/capacity publication
and element storage construction. Repeated cleanup and malformed function
alias targets are covered by focused negative tests.

## Deliberately retained contracts

| Retained state | Why it is not a dead machine C dependency | Exit condition |
| --- | --- | --- |
| `MinicCoreFunction.source_draft` / `core_ir_source.h` | During C-to-Core construction the parallel `MinicType` arrays preserve source-specific type identity until a transactional CoreType snapshot. Success releases the draft; on failure the importer can roll back/retry. The Backend rejects any published function retaining a draft. | Move the whole ingress transaction/sidecar lifetime together, prove failures and retries, then remove its CoreFunction attachment; never delete one pointer in isolation. |
| `MinicLocal.is_array` + `element_count` alongside materialized `MinicArrayType` | Frontend still creates both local/field array forms; mixed typedef dimensions, GNU zero-length objects, compound literals and VLA hidden bound/address slots use the old representation. Core lowering preserves pointer-to-array and one-time initializer timing. | Unify Frontend producers, expression/initializer/verifier/DataLayout consumers and Core lowering in one coherent array batch with positive/negative runtime/source tests, then retire the old branches. |
| Source GNU ASM C type and constraint admission | This legitimately examines C lvalues/types before Core is sealed; it is not a target machine readback of C AST. | Preserve strict fail-closed source admission and independent machine effects/ISA validation. |
| Different target frames, psABI and relocation spellings | RV32 ILP32, RV64 LP64, x86 SysV, AAPCS64 and Loong LP64D have real physical differences. | Share checked semantic facts only; do not force one cross-ISA physical algorithm. |

## Local proof and remaining acceptance boundary

These are distinct proof levels. The exact local integrated worktree passed:

| Gate | This batch's evidence |
| --- | --- |
| Debug `make MODE=debug -s -j8 check-fast` | **PASS** after building the missing `minic-as` prerequisite and rerunning the complete target, exit 0. This includes the new five-target SourceBackend registration check (five machine descriptors, three qualified source descriptors, two generic source descriptors, unknown target rejected). |
| Release `make MODE=release CFLAGS=-Werror -s -j8 build/release/bin/minic-cc` | **PASS**. |
| `bash tests/compiler/c0/check-minic-target-boundaries.sh` | **PASS** with the new source-versus-machine contract assertions. |
| CoreType native/ASAN/UBSan, Core verifier and Module rollback | **PASS**, CoreType native 4/4 and ASAN/UBSan 4/4. |
| New Core Module fragment contract | **PASS**, 19 positive/negative layout, overflow, symbol, relocation and alias-target checks. |
| RV32/RV64/x86 global source/ABI samples | **PASS** for the selected local partial gates only; not a broad Linux corpus or five-ISA ELF/QEMU acceptance. |

All PASS statuses in this table refer to local source/build/host probes after
integration, not to historical IR-P24 evidence or to a target-linux boot.

The IR-P24 PR-head CI contained successful Native Strict and selected
four-target jobs, but **not** five-target Linux runtime certification:
x86-64 `cmpxchg128` was unsupported, RV32 Zstd Core lowering and P2 virtio-gpu
failed, and AArch64 NEON Stage3 rejected a GCC pragma. Loong P2 had 17 real
cases plus one structural block. Success of an oracle *gate* with GNU/QEMU
execution jobs skipped is not an Image/P1 or P2 pass. The current architectural
batch has **not** rerun original full GNU Linux Image/ELF/QEMU five-target
runtime tests; these remain separate end-stage obligations after the Core/
Module/Backend boundary is stable.

This batch does not fix those runtime symptoms or add a universal MIR. The
next genuine architecture exit is to remove remaining old C-to-Core ingress
paths as complete dependency groups, then design a machine IR only after the
semantic Core boundary has been proved stable.
