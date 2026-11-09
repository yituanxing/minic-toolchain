# MiniC source-level audit — Common + x86-64

Baseline: `3866af2f64a8480268afa4655730d8334edfa4e7` (2026-10-29).

Status: **73/73 files read in full (Common + x86-64 C/H)**. This is a per-file reading ledger, not a claim that every defect has been discovered. `full` means all source lines examined during this review. These findings are a first full-code audit; subsequent dynamic differential/CI validation remains separate.

## Findings

- **P1, duplicated global classification:** `src/target/x86_64/global_emit.c:65-221` still classifies type, zero/bss section, records, arrays, relocations and storage itself, bypassing the new pure `minic_multi_global_emission_plan_build` in `src/target/common/global_emit.c:981-1110`. Preserve x86-specific relocation spelling and `float/double` payload support, but move shared classifications behind one checked plan before retiring the old branches.
- **P1, non-equivalent ELF section policy:** x86 `global_emit.c:127-132` emits `@progbits` for every user-supplied section, including `.bss.*`. Common `global_emit.c:960-970` uses `@nobits` for `.bss` prefixes. This is a concrete divergence to reproduce against GNU for named BSS/zero globals before changing emission.
- **P1, parallel x86 ASM dispatch:** `src/target/x86_64/asm_contract.c:8495-8753` first-matches dozens of source GNU ASM forms while `src/target/x86_64/machine_asm.c:5630+` independently matches textual forms for emission. Form identity is rederived instead of passed as a verified target-owned enum/ID. Migration must retain memory effects, early clobber, ABI registers and ELF alternative/exception records atomically; a blanket string passthrough would be unsafe.
- **P2, implicit semantics hidden at the Core boundary:** `src/core/core_lower_asm_target.c:187-203` promotes every target-qualified inline ASM to Core `is_volatile=true`, and ORs original `source->has_memory_clobber`, target `implicit_memory_effect`, and memory-RW operand kinds. This is intentional conservative semantics, but means source admission and machine guards cannot be compared by naive flag equality (e.g. `x86_64/asm_contract.c:6081` accepts source nonvolatile selectors; `x86_64/machine_asm.c:3912` requires Core volatile). Keep the normalized-target contract explicit in the planned typed form metadata and add a golden boundary test so future refactors do not accidentally remove implicit effects.
- **P2, borrowed CALL-index trust boundary:** `src/target/common/core_call_index.c:75-96` accepts a supplied `borrowed` index after structural count/pointer checks but does not cross-check its sites and views against the specified `function`. Production documentation assumes a verified visitor-owned index; this is safe only if that owner remains private. Consider stronger type/ownership API or debug contract test, not repeated production scanning.
- **P2, large-frame allocation complexity:** `src/target/common/core_object_reuse.c:80-109` greedily tests each object against all prior occupied object slots and their intervals. Worst-case nesting can approach cubic work even when liveness analysis already exists. Treat this as a measured performance risk, not yet a confirmed Linux wall-time bottleneck.
- **P2, transaction output permissions:** `src/target/common/program_emit.h:101-133` creates a fresh `mkstemp` output (typically mode `0600`) and renames it over the requested target. Atomic no-partial-output semantics are correct, but rewriting an existing mode-`0644` assembly file may silently change its permissions; decide whether to preserve old mode or rely on producer-private outputs.
- **P2, ABI planner public null-guard:** `src/target/common/direct_call_plan.c:219-231` checks `abi` before inspecting hidden return placement but does not check `indirect_record != NULL` before calling it for nontrivial record results. Normal direct-backend caller passes a callback, so the production path is safe; standalone/API misuse can crash instead of fail closed.
- **P2, x86 cached ELF alternative source templates:** `src/target/x86_64/asm_contract.c:1050-1111` accepts `CLWB` patch forms by source-length plus frozen FNV-1a hash and essential sentinels; `src/target/x86_64/machine_asm.c:989-1060` still performs a source-template-specific, tightly bounded replay/operand rewrite for one form. This is not a general raw-ASM escape and is not by itself a correctness defect, but leaves one target-owned exception to direct machine reconstruction. Retire it only after exact alternative/relocation byte differential evidence.

## Per-file reading ledger

| File | Read depth | Note |
|---|---|---|
| `src/target/common/aggregate_shape.c` | full | line-by-line first pass |
| `src/target/common/aggregate_shape.h` | full | line-by-line first pass |
| `src/target/common/core_block_walk.h` | full | line-by-line first pass |
| `src/target/common/core_call_index.c` | full | line-by-line first pass |
| `src/target/common/core_call_view.c` | full | line-by-line first pass |
| `src/target/common/core_call_view.h` | full | line-by-line first pass |
| `src/target/common/core_frame_layout.h` | full | line-by-line first pass |
| `src/target/common/core_function_facts.c` | full | line-by-line first pass |
| `src/target/common/core_function_facts.h` | full | verified-facts ownership contract |
| `src/target/common/core_function_plan.c` | full | one-time verifier/CALL/object ownership |
| `src/target/common/core_function_plan.h` | full | prepared function plan API |
| `src/target/common/core_instruction_family.h` | full | line-by-line first pass |
| `src/target/common/core_numeric_shape.c` | full | numeric overflow/bitscan shape predicates |
| `src/target/common/core_numeric_shape.h` | full | shared contracts |
| `src/target/common/core_object_intervals.c` | full | 432 lines escape/interval dataflow |
| `src/target/common/core_object_intervals.h` | full | indexed analysis lifetime |
| `src/target/common/core_object_reuse.c` | full | greedy reuse plan and complexity |
| `src/target/common/core_object_reuse.h` | full | reuse plan owner |
| `src/target/common/core_object_storage.c` | full | borrowed vs self-owned storage |
| `src/target/common/core_object_storage.h` | full | layout and lifetime API |
| `src/target/common/core_spill_layout.c` | full | 392 lines RV32/RV64 last-use allocator |
| `src/target/common/core_spill_layout.h` | full | spill allocation contract |
| `src/target/common/core_value_slots.c` | full | linear/wide value slot assignment |
| `src/target/common/core_value_slots.h` | full | value slot policy types |
| `src/target/common/direct_block_context.h` | full | read-only target block context |
| `src/target/common/direct_call_plan.c` | full | 526 lines of ABI placements and CALL cache |
| `src/target/common/direct_call_plan.h` | full | LP64 ABI cache interface |
| `src/target/common/direct_function_emit.h` | full | staged function emission and cleanup |
| `src/target/common/direct_function_plan.c` | full | 174 lines of shared LP64 function plan |
| `src/target/common/direct_function_plan.h` | full | function plan context |
| `src/target/common/direct_function_profile.c` | full | line-by-line first pass |
| `src/target/common/direct_function_profile.h` | full | line-by-line first pass |
| `src/target/common/direct_instruction_semantics.c` | full | 858 lines machine-neutral semantic gates |
| `src/target/common/direct_instruction_semantics.h` | full | shared Core checks API |
| `src/target/common/direct_linux_bug_asm.h` | full | exact ELF bug-table whitelist |
| `src/target/common/direct_lp64_abi.h` | full | AAPCS64/LP64D policy validators |
| `src/target/common/direct_machine_dispatch.h` | full | common machine control plane |
| `src/target/common/direct_preflight.c` | full | 250 lines preflight/CFG |
| `src/target/common/direct_preflight.h` | full | preflight API |
| `src/target/common/direct_program_emit.c` | full | transaction-backed TU adapter |
| `src/target/common/direct_program_emit.h` | full | direct TU emitter hooks |
| `src/target/common/direct_residual_plan.c` | full | 141 lines of ISA-neutral shape helpers |
| `src/target/common/direct_residual_plan.h` | full | residual semantic shape API |
| `src/target/common/elf_alias_emit.h` | full | 94 lines checked ELF alias emission |
| `src/target/common/global_emit.c` | full | 1189 lines of recursive initializers and global plan |
| `src/target/common/global_emit.h` | full | pure GlobalEmissionPlan API |
| `src/target/common/lp64_aggregate.c` | full | shared GP/indirect/FP record classifiers |
| `src/target/common/lp64_aggregate.h` | full | narrow shared ABI predicates |
| `src/target/common/lp64_frame.c` | full | 152 lines slot/frame calculation |
| `src/target/common/lp64_frame.h` | full | frame plan/bounds API |
| `src/target/common/lp64_scalar.c` | full | 88 lines target-width semantics |
| `src/target/common/lp64_scalar.h` | full | scalar classifier API |
| `src/target/common/program_emit.h` | full | output temp transaction and stage owner |
| `src/target/common/program_visit.h` | full | semantic plan's unique lifetime owner |
| `src/target/x86_64/abi.c` | full | 262 lines ABI record classifiers |
| `src/target/x86_64/abi.h` | full | ABI public API |
| `src/target/x86_64/asm_contract.c` | full | 8,753 lines source-form matching, GNU constraints, flags and target qualifier dispatch |
| `src/target/x86_64/asm_contract.h` | full | named GNU ASM form API/ISA descriptors |
| `src/target/x86_64/call_plan.c` | full | SysV/Win64 incoming/outgoing CALL plans |
| `src/target/x86_64/call_plan.h` | full | ABI cache and placement types |
| `src/target/x86_64/codegen.c` | full | verified visitor, native function prologue/epilogue |
| `src/target/x86_64/codegen.h` | full | TU API |
| `src/target/x86_64/frame.c` | full | line-by-line first pass |
| `src/target/x86_64/frame.h` | full | frame ownership and offsets |
| `src/target/x86_64/global_emit.c` | full | line-by-line first pass |
| `src/target/x86_64/global_emit.h` | full | old target global API |
| `src/target/x86_64/machine_asm.c` | full | 6,472 lines physical Linux ASM semantics, forms/ELF patch and extable records |
| `src/target/x86_64/machine_call.c` | full | 631 lines of SysV/Win64/x87 CALL emission |
| `src/target/x86_64/machine_emit.c` | full | 1421 lines of Core→x86 machine cases |
| `src/target/x86_64/machine_emit.h` | full | machine API |
| `src/target/x86_64/machine_fp.c` | full | 200 lines SSE/x87 scalar emission |
| `src/target/x86_64/machine_internal.h` | full | private machine primitive API |
| `src/target/x86_64/machine_support.c` | full | 140 lines of physical stack/GP primitives |

## Architecture/retirement proposal (audit-derived)

1. **Preserve distinct ownership boundaries.** Shared Core FunctionPlan/Facts (`common/core_function_plan.c`, `common/program_visit.h`) owns semantic verification, CALL index and object lifetime once. Target ABI (`x86_64/abi.c`, `call_plan.c`, `machine_call.c`) alone owns SysV/Win64 placements, callee-save discipline, aggregate rules and x87 returns. No reason to duplicate the verified Core semantic scan in every x86 function emitter.
2. **Make GlobalEmissionPlan a completed decision layer.** Common planner should own generic global storage/section, zero-fill and initializers once; x86 target should own ELF dialect and payload/relocation encoding. Move named `.bss*` section policy into a single documented common/target contract, verify GNU object attributes, and remove the duplicate target classification branches in one batch.
3. **Carry target-owned ASM form identity without adding x86 cases to Core IR.** Qualify source into a backend-private immutable sidecar keyed by the generic `inline_asm_id` (or equivalent target callback state). Carry form ID, exact GNU constraint fingerprint, flags/implicit effects and source provenance to machine emission. Then the machine side validates typed operand widths/roles and emits exact hardware instructions and ELF metadata; it need not reclassify thousands of bytes of template text. Remove the old text-dispatch path only after source/machine agreement and Linux 6.6 `__ex_table`, `.altinstructions`, `.smp_locks` relocation differential tests.
4. **Protect known working ABI paths.** Keep SysV and Win64 independent policy variants behind one interface. Keep complete 64-bit scalar, aggregate, FP/x87, register-save, hidden return and call alignment tests. Avoid treating Win64 as a small SysV exception.
5. **Measure before optimizing.** Worst-case local-interval slot reuse and temp-output mode behavior should get isolated tests; neither warrants speculative producer-wide rewrites before measured evidence.

## Verification scope and limits

- All 73 scoped C/H files were read completely, including 8,753-line source ASM admission and 6,472-line machine ASM. This is an architectural code review, **not** a fresh 7-way Linux compile/runtime verification.
- Pure source review did not establish x86 global section divergence as a GNU behavioral failure; the classification duplication and conflicting named-BSS policy are verified code facts. The exact correct ELF policy needs differential testing.
- Source-form `is_volatile` versus Core `is_volatile` differences are **not** evidence of a bug: target lowerer deliberately normalizes Core forms to always volatile. This was cross-checked before reporting.
