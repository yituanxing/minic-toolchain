# MiniC architecture audit: keep the single Core, retire the parallel paths

Date: 2026-10-29. This is a source-ownership and refactor plan, **not** a
machine-code or runtime-correctness certification. It deliberately excludes
MiniAS, MiniLD, MiniPP, and NaSeed.

## Immutable acceptance starting point

The pre-refactor tree is `c3c1dbf7c` locally (the same tree as published
`ca7e995697cd47bf5fb29e0b16a6bf7da58f59fd`). The preceding MiniC
three-ISA GNU Linux 6.6.143 campaign passed 21 independent jobs and its
summary with no remaining compiler diagnostic groups:

| Target | Original frozen GNU preprocessed C inputs | MiniC `-S` | Shards |
| --- | ---: | ---: | ---: |
| x86-64 | 2758 | 2758 | 7 |
| AArch64 | 4078 | 4078 | 7 |
| LoongArch64 | 2723 | 2723 | 7 |
| **Total** | **9559** | **9559** | **21** |

The source set is made from the GNU Kbuild broad-profile owner census and
the frozen, SHA-verified `.i` corpus. The original command and source
provenance is checked by `linux-three-isa-golden-v1.py` and
`linux-three-isa-freeze-i-v1.py`, and each shard is executed by
`linux-three-isa-seven-probe-v1.py`. The authoritative workflow is
`.github/workflows/minic-linux-three-isa-discovery.yml`; it requires a
campaign trigger change or an explicit workflow dispatch. **Do not** replace
this evidence with a selected subset, changed Linux Kconfig, a skipped
diagnostic, or a denominator produced from the modified compiler.

Passing `MiniC .i -> .s` does **not** demonstrate GNU AS acceptance,
linkability, ELF/psABI correctness, or QEMU execution. Those remain separate
post-architecture milestones.

An independent four-architecture runtime workflow on that same pre-refactor
commit had **three pre-existing failures**: x86-64 typed-overflow builtins,
AArch64 AAPCS64 `x8` large-record indirect result, and LoongArch64 LP64D
hidden-`a0` record result. The post-refactor run at `f3eda60e8` reproduced
those same three failures; RV64 passed. These are baseline defects, not proof
of regressions from the architectural batches; compare exact runtime steps
and diagnostics when assessing later changes.

## Exhaustive source inventory for the review

The file-level architecture review covers all 319 tracked C/header files
under `src/`. Counts below are derived from the tracked path inventory,
not inferred from six hand-written per-agent totals:

| Ownership area | Tracked C/H files | Current responsibility |
| --- | ---: | --- |
| `frontend/` | 70 | C parsing, source types, AST and GNU extensions |
| `compiler/` | 11 | AST/Core pipeline, reachability and driver integration |
| `core/` | 43 | Target-neutral semantic IR and lowering |
| `target/` top-level | 10 | TargetInfo, DataLayout, catalog and backend interfaces |
| `target/common/` | 54 | Verified function facts, visitors, output transaction and selected shared plans |
| `target/x86_64/` | 19 | SysV/Win64 ABI and native x86 lowering |
| `target/aarch64/` | 22 | AAPCS64 and AArch64 instructions |
| `target/loongarch64/` | 22 | LoongArch LP64D and instructions |
| `target/riscv/` | 10 | RISC-V shared ISA and ABI primitives |
| `target/riscv32/` | 16 | RV32I/ILP32 machine implementation |
| `target/riscv64/` | 42 | RV64 psABI and machine implementation |
| **Total** | **319** | |

The CLI tools `tools/minic/main.c` and `tools/minic-cc/main.c` are also
part of the target/driver boundary review. A path-level inventory and
symbol/call-chain review do not amount to line-by-line correctness proof.

## Preserve these already-correct boundaries

1. All five backends already pass through `minic_program_emit_write` for
   transactional output and through `minic_core_function_plan_build` for
   verified Core facts, CALL indexing and storage information. There are
   **not** five independently implemented compilers or two complete Core IRs.
2. AArch64 and LoongArch64 share a downstream Direct LP64 function plan,
   but **not** the same parameter-placement ABI. Their aggregate splitting,
   hidden result pointer, `__int128`, varargs and save areas genuinely differ.
3. RV32 ILP32, RV64 LP64/LP64D, x86-64 SysV and AAPCS64/LoongArch LP64D
   have different physical frame and call layouts. Common owns semantic
   proofs and indexes; the target owns registers, offsets, instructions,
   relocations and any required strict-ISA audit.
4. Existing GNU inline-ASM allowlists are deliberately fail-closed. An
   architectural cleanup must never turn an unrecognized source template
   into arbitrary pass-through machine ASM.

## Prioritized structural debt and required retirement

| Priority | Problem, with source ownership | Migration and removal criterion |
| --- | --- | --- |
| P0 | `core_lower_asm.c` tries overlapping GNU ASM handlers; target-qualified `core_lower_asm_target.c` loses `early_clobber` that `core_lower_asm_structured.c` preserves | One operand/effect plan consumed by all relevant target qualifiers and emitters; remove replaced handler branches only after positive/negative GNU constraints tests |
| P0 | `loongarch64/simd_asm.c` scans every frontend inline ASM while emitting each SIMD Core instruction | Preserve an optional, verified Core `source_inline_asm_id`; direct lookup plus full effect proof; delete the per-instruction whole-TU scan |
| P0 | `aarch64/asm_legalize.c` reparses and rewrites complete emitted assembly for frame offsets and dynamic alloca | Native AArch64 `FrameRef`/address/scratch policy; convert every affected family and finally remove text rewrite callbacks and implementation |
| P0 | `common/direct_call_plan.c`, `direct_function_profile.c`, `direct_function_plan.c`, `lp64_frame.c` scatter AAPCS64-versus-Loong ABI branches | Explicit target ABI policy passed once through shared planning; remove migrated branches and old two-valued enum ownership without merging real ABI differences |
| P1 | x86 `asm_contract.c` and `machine_asm.c` maintain overlapping first-match dispatch for Linux ASM, with ELF alternative/exception metadata | Target-owned typed form identity: recognize once, verify Core roles and emit under the same form; delete each corresponding old matcher/emitter dispatch atomically |
| P1 | Frontend `type.h`, `parser_function.c`, `ast_verifier.c` enumerate x86 cursor and AArch NEON/SVE header types | Target-described nominal extension identity, declarative type/category capabilities, shared type verification; remove ISA-named frontend enums while preserving unsupported layout failures |
| P1 | `target_info.c` combines five ISA-specific register/ASM/builtin rules; catalog, backend and layouts register targets in several places | Target-owned Asm/Builtin/VariadicPolicy and registration consistency tests; keep thin driver catalog separately linkable |
| P1 | Global initialization/ELF classification repeated in RV32, RV64, x86 and `common/global_emit.c` | Common checked GlobalEmissionPlan followed by native dialect callbacks; remove only duplicate classification, not native ELF/relo handling |
| P1 | Core legacy `is_array+count` and materialized array types coexist; block-local SSA value transport special-cased | Unify representation at AST-to-Core boundary, keep block-local SSA contract explicit; remove obsolete fallback after all consumers migrate |
| P1 | RV32/RV64 frame scratch and machine-ISA checks differ; RV64 source register can conflict with large-offset address scratch | Explicit scratch constraints, complete FunctionSymbol initialization, later shared RISC-V generated-ISA verifier parametrized by XLEN |
| P2 | 90 stale architecture review source fingerprints in the `check-minic-target-boundaries.sh` ledger after the first batch | Refresh only with reviewed file evidence; stale fingerprints are a maintenance failure, **not 90 compiler bugs** |

Two implementation hazards must not be misclassified as dead code:

- Common RV spill reuse and Direct LP64 fixed/wide-tail slots implement
  distinct physical allocation policies; both have active callers.
- `reentry_index` fast lookup and Core's conservative global scan are
  currently correctness fallbacks. Migrate through one conservative query
  interface before deleting the old scan.

## Refactor batches and gates

1. **First integration:** target-specific ABI policy for AArch/Loong;
   Core ASM provenance and the Loong SIMD scan retirement; x86 parameter
   register policy; RISC-V frame scratch ownership. These touch disjoint
   source owners and are integrated in one reviewed MiniC tree. AArch64
   text-postprocessing removal is a separate, larger native-frame batch.
2. **ASM contract consolidation:** typed operand/effect plan with early
   clobber and source mapping, target form identity and old-handler deletion.
   The complete Linux template families and ELF metadata must remain
   fail-closed.
3. **Language-boundary cleanup:** target extension types and attributes,
   unified Core array representation and source facts, then global object
   plans. Replace existing implementations instead of accumulating shims.
4. **After architectural stability:** independent correctness/psABI/ELF,
   static-link and real QEMU checks. A `.s` success must not stand in for
   those results.

Every batch must report changed files, added/deleted lines, old code removed,
remaining compatibility paths, local strict `-Werror` build, focused positive
and negative probes, and one **same-configuration exact-head 21-shard** GNU
Linux `.i -> .s` census before claiming preservation of 100%.

For the first integrated batch, the focused source and local-ABI gates cover:
Core source-ID provenance including ID zero; direct AArch64/LoongArch64 LP64
policy and fail-closed named variadic floating arguments; 128 misleading
LoongArch SIMD ASM templates; x86 SysV/Win64 call register placements; RV64
static/dynamic large-offset word and `__int128` pair accesses with live
register preservation. The RISC-V physical-register alias normalization
(`t2` versus `x7`) and multioperand inline-ASM liveness remain a subsequent
architectural review, not an implicitly accepted property of this batch.

Do not overwrite the older dirty `minic-toolchain` worktree when integrating
the reviewed changes; its uncommitted and untracked files are independent
user work. The MiniC-only development branch is
`refactor/minic-target-language-boundary`.

## Second architecture integration: boundaries retired, semantics retained

The next reviewed tree starts from the published first-batch head `cc3f27767`.
Its change set owns six independent architectural concerns:

1. Core: remove eight unreachable GNU structured-ASM shape handlers and
   their dispatch prototypes, while retaining all four legacy SSA-result
   opcodes still consumed by RV32/RV64. Target qualification, memory effects,
   fixed registers, early-clobber and `asm goto` retain their existing
   admission order.
2. TargetInfo/frontend: replace five lazy per-target initialization branches
   with one declarative language-policy table and three *C integer models*.
   The five distinct machine ABIs remain independent. Move AArch64 opaque
   vector header-return permission out of the parser; normalize RISC-V
   register aliases to target-owned physical identity without widening RV32
   source-ASM admission.
3. AArch64: add native typed FrameRef address, memory-width and scratch
   policy, migrate FP and direct memory SSA frame operations, and test large
   displacement and dynamic `x19` anchor hazards. The legacy assembly-text
   legalizer is intentionally **still active** for integer, call, block and
   qualified inline-ASM paths, and must not be deleted this batch.
4. x86: replace redundant GNU SMP/local/percpu XADD and GS CMPXCHG recognizers
   with one target-private typed atomic decision, preserving each variation's
   separate operand proof and native emitter validation.
5. RISC-V: normalize `xN`/ABI/fp aliases in fixed/clobber/scratch decisions,
   reject over-capacity RV64 ASM plans before indexing a ten-slot buffer,
   and preserve live data/address registers across large-frame chunk access.
6. Common globals: validate size/alignment, ELF classification and nine
   initializer payload shapes into one pure GlobalEmissionPlan, then dispatch
   output. ISA-specific relocation callbacks and legacy backend-local global
   classification are not collapsed yet.

The local strict `-Werror` MiniC compiler build and focused tests (typed A64
frame, x86 GNU-native GS atomics including four negative cases, RISC-V
physical alias/frame, five-target model, target-language options, Core ASM
fail-closed census, five-ISA operand-capacity rejection) pass. The independent
three-ISA **exact-head 21-shard frozen GNU Linux replay remains the required
post-push acceptance**, and successful C-only emission must never be relabeled
as GNU AS, ELF link, kernel boot or QEMU runtime certification.

Two older RV inline-ASM unit scripts hard-code physical temporary registers
(`t1/t0` and `t3`) that already differed on the pre-batch compiler binary;
those are stale textual fixture expectations, not evidence of a new compiler
semantic regression. Their checks should eventually assert operand semantics
and constraints instead of accidental register allocation names.

## 2026-10-30 exact-head GNU Linux link/runtime blockers (active)

The following ledger is from PR #674 head `d81261dda21d`, **not** a
replacement for the earlier `.i -> .s` 21-shard result. Original GNU Linux
6.6.143 was built at each target's frozen Kconfig and boot-tested in QEMU;
all candidate C objects must be built by MiniC and assembled/linked by GNU.
The authoritative three-ISA run is GitHub Actions `41020466906`; the RV64
full-C campaign is `41020466920`. Preserve the exact input/command/hash
provenance and do not mix GCC-produced C objects into the candidate image.

| Priority | Target / stage | Evidence and current blocker | Next proof required |
| --- | --- | --- | --- |
| P0 | x86-64 full GNU relink | 2758/2758 MiniC `.s -> GNU AS .o` pass. GNU link has unresolved `__compiletime_assert_*`, `__field_overflow`, configuration-gated functions, CFI and kallsyms references; executable sections grow from 16,075,966 to ~351 MB (21.84x). | Classify all unresolved references by Core/AST reachability, source attributes, configuration and generated sections; retain side effects and all true refs, then rerun all 2758 objects, link and QEMU P0/P1. |
| P0 | LoongArch64 full GNU relink | 2723/2723 MiniC objects pass. GNU `head.o` original `.ref.text` branch to `start_secondary` overflows `R_LARCH_B26` by a -266,461,448-byte displacement after MiniC `.text` grows to ~267 MB (16.11x GCC executable bytes). This instruction is in original GNU assembly, not MiniC CALL lowering. | Reduce real machine-code size / preserve legal branch reach without changing Linux source, Kconfig, GNU LD or GCC fallback; retest 2723/2723 + GNU Image + QEMU. |
| P0 | RV64 exact same-Image repeated P1 | 2064/2064, GNU full link and one `full_all_verified` QEMU P1 with 12 syscalls pass. The separate 12-process runner used QEMU's default `-initrd` at 0x88200000, overlapping a 150,491,136-byte Image; no repeated process passed. | Reuse the successful exact-Image custom DTB, initrd 0xa0000000 and 2 GiB map in each immutable-Image boot; certify 12 independent processes, each P1 12/12. |
| P0 | Native strict output transaction | CI `41020466986` stopped on a stale positional `MinicProgramEmitConfig` test initializer and then synthetic no-CFG link/test setup. | Exact-head `check-minic-target-boundaries.sh` and complete native strict CI green. |
| P0 | AArch64 full candidate | Original GNU Linux P0/P1 PASS; 4108/4108 MiniC C owners passed GNU AS. GNU Kbuild relink rc=2 on many `__compiletime_assert_*`, `kallsyms_*`, `__start_BTF`/`__stop_BTF`, `efi_arch_tables` and EFI attribute symbol references. No MiniC Image or QEMU candidate P0/P1 yet. The sole vDSO diagnostic complaint is an ambiguous `vgettimeofday.o` GNU `-S` input mapping, explicitly diagnostic-only, not an assembler failure. | Trace surviving undefined references to the exact Core or global source owner and determine live versus dead/config-excluded paths. Fix only proven MiniC frontend/Core/target defects; rerun 4108 owners + GNU link + QEMU P0/P1. |

The LoongArch and x86 size deltas show **cross-target code materialization
debt**, not permission to apply a Loong-only linker workaround. Common's
Direct machine dispatcher writes every live SSA result into a frame slot,
and target families reload operands. A first experiment skipping zero-use
stores did not reduce the real LoongArch `vgettimeofday` fixture (0%); a
verified single-pass use/materialization plan with target-owned register
forwarding and conservative call/ASM/CFG invalidation is the next design
candidate, not yet a correctness-verified fix. Existing Core spill-layout
ownership, function lifetimes and ABI-specific frame rules must be reused;
do not add a parallel algorithm or remove old code without proving its
replacement covers all actual call sites.

### Same-day isolated fixes entering the next whole-Linux CI

* **x86 CFG ownership:** Unlike the other four backends, the x86 function
  walker unconditionally visited all Core blocks and its backend contract did
  not request reachable-only emission. The actual Linux undefined references
  include calls produced solely from unreachable blocks, not just a missing
  external library. The three matching contracts (backend feature bit, Core
  block-walk reachability mask, live-symbol scan) are now aligned. An isolated
  GNU AS/LD/runtime fixture proved `mesh_update` and
  `__compiletime_assert_212` disappear as undefined references, while
  externally entered source labels and real side effects stay executable.
  The existing x86 lowering still rejects a dynamic `goto *ptr` independently
  of this change; this fixture does not certify computed-goto runtime support.
  Full 2758-owner CI is the remaining proof.
* **Loong exact frame provenance:** Native aligned loads/stores can consume
  a proven static Core frame-object+field offset directly. Address producers
  are elided only when every Core use is certified to be such a memory
  consumer; escape, asm, unknown provenance, unaligned and wide cases retain
  the existing path. GNU Linux vDSO samples shrank by ~37% in isolated
  compilations without changing the linker/assembly tools. This is a
  code-size intervention, **not yet evidence** that the B26 link overflow is
  gone.
* **x86 Common spill allocation:** x86 now consumes the existing Core
  block-local last-use spill layout instead of assigning a monotonically
  increasing slot to every SSA result. The target still owns x86 SysV
  negative-`rbp` displacement, 8/16-byte values, and preserved-register ABI
  placement. A real Linux vDSO source plus GNU AS shrank `.text` from 42,961
  to 31,188 bytes (27.4%), with GNU/MiniC native runtime and x86 long-double,
  `__int128`, Win64/SysV and variadic tests passing. Whole-Linux link/runtime
  correctness remains to be established by the exact-head CI.
* **AArch vDSO evidence:** The diagnostic-only source resolver mistook the
  `.c` argument of Kbuild's `-include` for a second translation-unit source;
  the parser now excludes `-include` and `-imacros` operands and self-tests
  the exact two-input form. This removes a false diagnostic and does not
  resolve the independent AArch full-kernel GNU linker failures.
* **Cross-target ELF weak undefined owner:** Actual AArch Linux ELF link
  diagnostics include EFI objects and functions intentionally declared
  `extern __weak`, which must resolve to zero if no definition is linked.
  Existing emission of an ordinary strong undefined reference changed that
  contract. Live undefined weak binding now belongs to the same Common
  symbol-use emission pass as undefined ELF visibility; target-specific
  definition emission remains separate. GNU AS/LD ELF binding and runtime
  checks distinguish absent weak declarations, hidden weak references and
  a strong provider. This fixes a concrete class of five AArch references,
  not the remaining compiletime-assert/kallsyms/BTF linker failures.
* **RV64 repeat loader:** The failing repeated-QEMU runner now uses the
  custom DTB/initrd mapping already certified by the one successful
  full-C process when an image exceeds 128 MiB. The exact-Image 12-process
  CI must still pass before its stability result is claimed.
