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
commit also had **two pre-existing failures**: x86-64 typed-overflow builtins
and LoongArch64 LP64D hidden-`a0` record result. These are baseline defects,
not proof of regressions from the architectural batches; distinguish them
explicitly when comparing future runtime jobs.

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
| P2 | 83 stale architecture review source fingerprints in `check-minic-target-boundaries.sh` ledger after full-coverage changes | Refresh only with reviewed file evidence; stale fingerprints are a maintenance failure, **not 83 compiler bugs** |

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
