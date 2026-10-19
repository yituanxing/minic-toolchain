# Two-backend architecture audit — 2026-10-19

## Scope and evidence

This is a **source inspection and decision record**, not a new runtime certification, optimization result, or refactor proposal. Reviewed the multi-target feature snapshot `c5da00c652b002ff07665d211fd3d9c4a3e39836` against `main@2102a0894110ff3af7d6cf5e67811dc561f063d2`, including the compiler dispatcher, frontend type model, Core instruction interface, target/data-layout interfaces, both emitters, RV64 layout, staged driver, and the existing real-project acceptance report.

Primary evidence:
- `src/compiler/compiler.c:3116-3276`: one selected `MinicTargetInfo` flows through parse, verification, specialization, Core lowering and target dispatch.
- `src/target/target_info.{h,c}`, `src/target/data_layout.{h,c}`: integer models, type layout, builtins and feature boundaries are explicit.
- `src/core/core_ir.h:100-139`, `src/core/core_lower.c`: typed varargs operations and atomic effects are preserved as Core semantics rather than replaced by unsound ordinary memory operations.
- `src/target/riscv64/core_codegen.c`: explicitly rejects unsupported x86-shaped vararg/atomic Core operations. `src/target/x86_64/codegen.c` owns SysV register/stack classification, x87, int128 slots, relocations, argument passing and assembly syntax.
- `tools/minic-cc/main.c` accepts the target; `tools/minic/main.c:525-536` deliberately refuses an x86 pipeline that the surrounding native Mini tools do not support.
- Real-world scope and CI receipts: `docs/architecture/multitarget-x86_64-real-project-checkpoint-2026-10-18.md`. In particular: SQLite 3.54.1 database runtime, Lua 5.5.0 interpreter + luac, zlib 15/15, MiniC-built TinyCC + 36 original tests2 programs, nine mixed MiniC/GCC BusyBox applets.

Most recent relevant scoped evidence before this audit: focused [#39178856257](https://github.com/yituanxing/minic-toolchain/actions/runs/39178856257) succeeded on `5e2bc21451`. Subsequent `c5da00c652` restores the focused workflow and adds documentation; it is not a new full Linux qualification. Historical **fresh** 2064-object RV64 Linux/QEMU P1 green exists at earlier feature commits, but **not at this exact HEAD** after further shared frontend changes. An unrelated pre-existing malformed `linux-runtime-gcc-baseline.yml` dispatch failure is not proof of compiler regression.

## Result: keep the Core / target boundary, do not extract Machine IR today

The second backend has successfully reused the source parser, C semantic/type machinery, Core CFG and operations, and the existing target-aware DataLayout. ABI lowering and instruction encoding remain target-owned. This is already a meaningful multi-target boundary.

**Decision:** retain the direct Core -> RV64 / x86-64 emission pipelines for now. Do not introduce a generic Machine IR, generic physical-register allocator, instruction-table DSL, or plugin registry merely because there are two targets. The current x86 emitter deliberately uses conservative per-Core-value stack slots and target-specific 8-/16-byte widths; that makes it slow or verbose in some cases, but the available correctness evidence does not justify a cross-backend machine layer.

This is compatible with a *future* optimizer: semantic Core passes can be added before target lowering without machine registers; target-specific peepholes can be added after lowering if the emitter later acquires a suitable representation. A full machine IR becomes worth considering when two measured backends need the same post-selection analyses/rewrites or an actual register allocator. Avoid promising feature-complete optimization support until pass invariants and invalidation behavior are specified.

## Specific findings, ordered by risk and action

| ID | Evidence / concern | Classification | Action |
| --- | --- | --- | --- |
| F1 | `MINIC_TYPE_BASE_X86_VA_LIST_CURSOR` is a target-named case in the common `MinicTypeBaseKind` (`src/frontend/type.h:30-34`). The current x86 implementation carries an 8-byte cursor proxy, while the genuine SysV `__builtin_va_list` is a one-element array of a 24-byte structure. `minic_target_info_sizeof_type` correctly rejects misleading `sizeof` instead of fabricating 8. | **Concrete representation debt; safety guard currently present.** | Preserve fail-closed behavior. When an unchanged real project needs `va_copy`, address-of, array/decay or sizeof behavior, implement a target-constructed semantic va_list type with verified storage/parameter rules. Do not generalize the pointer proxy or alias it to `void*`. |
| F2 | `minic_compile_preprocessed_file_for_target` explicitly branches on RV64/x86 for validation and emission (`src/compiler/compiler.c:3137-3143,3251-3276`). `TargetInfo` currently holds data/capabilities, not emitter hooks. | **Appropriate two-target switch; expansion seam.** | Leave the switch. On the **third real** backend, or if dispatch gets materially more stages, consider a small registered TargetBackend descriptor for `validate`/`emit` and a target key, retaining DataLayout/ABI ownership; avoid a kitchen-sink target vtable. |
| F3 | The RV64 path additionally executes `minic_riscv64_layout_program`; the x86 path performs its frame/global layout in its backend (`src/target/riscv64/layout.c:201-217` and `src/target/x86_64/codegen.c`). | **Asymmetric validation staging, not demonstrated incorrectness.** | Document that the RV64 stage checks record/global representability while x86 validates during emission. Consider a common `target validate` *contract* only when a real failure requires consistent pre-emission diagnostics; do **not** move machine frame slots into Core. |
| F4 | The x86 emitter currently keeps frame allocation, SysV argument classification, symbols/relocations, global initialization, per-instruction generation and emission in one `codegen.c` (~2,438 lines), while RV64 already uses separate ABI/layout/codegen files. | **Maintainability risk, no measured correctness regression.** | Any near-term split should be internal to `src/target/x86_64/`: `abi`, `frame`, `data/elf`, and `emit` only when source changes repeatedly touch the same subsystem. Keep the public x86 emitter interface stable and rerun targeted real-project oracles after each small split. |
| F5 | `src/compiler/compiler.c` performs inline integer/symbolic/boolean specialization and reachability pruning before Core codegen (`~3192-3245`); there is not yet an explicit independently scheduled, analysis-invalidating optimization pass protocol. | **Future optimization extension seam; not a current defect.** | Before implementing a *second configurable* Core optimizer, define the narrow pass contract below. Do not prematurely reorganize the existing optimizations just to expose user-facing `-O` options. |
| F6 | `include/minic/compiler.h` still says C0 only accepts `int main` and that x86 target intentionally fails closed until a backend exists, although source and project tests prove otherwise. | **Stale API comments only.** | Update documentation near the eventual feature merge; no behavior change or need for a dedicated CI run. Clarify native `minic-cc` versus staged `minic` pipeline. |
| F7 | `tools/minic/main.c` refuses x86 even though `minic-cc --target=x86_64-linux` works. MiniAS/MiniLD/MiniPP are currently RV64-focused. | **Intentional product boundary.** | Continue GNU preprocessor, assembler, linker and CRT for current x86 project qualification. Treat an x86 end-to-end Mini driver and MiniAS/MiniLD support as separate milestones; do not equate x86 C object success with native-toolchain self-hosting. |

### Why this boundary is consistent with GCC and LLVM

GCC's target interface uses target hooks and target machine descriptions to own machine properties and code generation decisions: [GCC Target Structure](https://gcc.gnu.org/onlinedocs/gccint/Target-Structure.html). LLVM explicitly separates `DataLayout` from target legalization, calling convention and instruction selection through `TargetLowering`/target descriptions: [LLVM Code Generator](https://llvm.org/docs/CodeGenerator.html). LLVM additionally has machine-level IR because it supports target-independent machine passes, register allocation and multiple instruction selectors: [GlobalISel](https://llvm.org/docs/GlobalISel/index.html).

**Import the separation of responsibilities, not the implementation scale.** MiniC does not yet have evidence that sharing physical-register assignment or instruction selection between RV64 and x86-64 would be net beneficial.

## Future opt-in optimization contract — design criteria, not implementation

When an observed optimization workload demands configurable sequencing, the minimal Core pass should declare:

1. **Input contract:** verified `MinicCoreFunction` CFG/value/type/side-effect invariants; explicit `MinicTargetInfo` only when a transform genuinely depends on semantic widths.
2. **Transformation boundary:** mutate Core instructions/blocks only through invariant-preserving helpers; never modify backend stack offsets, physical registers, or assembler text in Core.
3. **Analysis ownership:** declare analyses consumed and invalidated (dominance, reachability, value-use, call effects, alias/memory effects as actually implemented); no reuse of stale cached results.
4. **Safety:** preserve volatile/atomic/compiler barrier effects, exact operand evaluation count, ABI-visible types, source-level undefined-behavior assumptions and fail-closed feature restrictions.
5. **Control:** explicit on/off and deterministic order, independent test of each candidate pass. No global `-O2` alias before there is measured evidence.
6. **Measurement:** isolated candidate vs baseline on the **same frozen input**, report correctness oracle, affected TU count, aggregate and per-TU wall/CPU time, peak RSS, output/code size and performance variance; accept only when correctness and attributable gain hold.

This makes an optimizer *addable later* without requiring an unvalidated plugin host now.

## Gate before final PR merge or further Linux escalation

1. Keep real-project oracles authoritative: untouched pinned tiny-AES/cJSON, full zlib gzip/minigzip differential, Lua interpreter/luac PIE and non-PIE, SQLite database oracle, TinyCC 36-program subset, and BusyBox nine mixed applets. Record the exact SHA and run.
2. Complete a **fresh full RV64 Linux 6.6.143** 2064/2064 Kbuild MiniC compile + GNU link + QEMU P1 on the final intended source content (including the latest shared frontend fixes). Use the existing seven-shard workflow and SHA-based owner/manifest verification; don't re-use older objects as if they covered changed source. Keep Stage4 nine-tool self-hosting a separate claim.
3. Isolate the legacy invalid baseline workflow as a CI maintenance issue; don't turn it into a compiler blocker and don't permanently change YAML solely to make a benchmark green. Revert temporary run-specific YAML changes to the expected baseline when done.
4. For any architecture adjustment, capture: observed real source / GCC differential -> ownership diagnosis -> minimal change -> focused target regression -> cross-target regression -> full gate when warranted. Track every fail-closed previously unsupported construct explicitly.

## Immediate next work

Prefer **small, real-project-driven pressure** (e.g., increment BusyBox's unchanged C owner selection or TinyCC's original tests2) over a generic framework rewrite. In parallel, use this audit to label new failures `Frontend/Sema`, `Core semantic/optimization`, `Target ABI`, `Target machine emission`, `Driver/toolchain`, or `Test harness/configuration` so the third real backend will inherit evidence-backed seams.

**Not changed by this audit:** compiler behavior, CI workflows, main branch, existing real-project oracle scripts. The current status and limitations above remain in force.
