# C Rewrite Architecture Audit — 2026-08 / C 重写架构审计

Scope: current C compiler structure plus the active Lua/Linux discovery frontier. This audit evaluates whether present choices leave room for IR, optimization, multiple C dialects/GNU extensions, native toolchain stages, in-memory execution, debug information, and self-hosting.

## Verdict / 结论

The current C compiler has a reusable core and should **not** be rewritten from zero. Its strongest permanent decisions are the split frontend modules, stable IDs, Program-owned storage, explicit Parsed/Normalized verification, and a distinct RV64 target directory.

The main architectural risk is not the existing C core itself. It is allowing temporary discovery mechanisms and correctness-first data representations to become permanent while Linux coverage grows.

Therefore the project is at a hardening checkpoint: preserve behavior, repair boundaries, then insert IR. This is a structural refactor of ownership and representation, not a semantic restart.

## A. Decisions that should survive / 应保留

### A1. Stable IDs and Program-owned pools — KEEP

Expression/local/statement/block/function/record identities are stable IDs rather than raw pointers into reallocating storage. This is compatible with arenas, compaction, side tables, IR mapping, serialization, and deterministic dumps.

### A2. Split parser/frontend files — KEEP, then unify declarators

The parser is already decomposed by concern instead of becoming one giant Python-like file. The missing abstraction is declarator construction shared across globals/locals/parameters/typedefs/record fields/type names.

### A3. Parsed -> Normalized verification — KEEP and generalize

The verifier pattern should be copied forward:

```text
Parsed AST verifier
Normalized AST verifier
Core IR verifier
Machine IR verifier
Object/link consistency checks
```

Verification between representations is more valuable for future optimization than relying on codegen failures.

### A4. Target directory — KEEP, strengthen boundary

The target directory is the right direction. The next step is to stop target layout from mutating semantic frontend objects and provide explicit TargetInfo/DataLayout/ABI interfaces.

## B. Temporary mechanisms that must not survive / 必须收口

### B1. Ordered Python source rewriting — CRITICAL

The Lua/Linux frontier currently builds an effective compiler by applying many ordered Python text patches before compiling C source. This is acceptable only as a discovery notebook.

Failure modes already observed include identical textual anchors appearing in unrelated functions, patch-order sensitivity, and generated-source corruption such as an accidental NUL byte.

Exit criterion:

```text
git checkout <consolidated branch>
=> checked-in include/src/tools sources are exactly the compiler CI builds
=> no semantic source-rewrite stage is required
```

Discovery branches remain immutable archaeology after materialization.

### B2. Target layout stored in semantic AST — HIGH

Current records/locals/functions carry storage offsets/sizes that RV64 layout fills in. This makes one semantic program implicitly tied to one target layout and complicates multi-target compilation, IR reuse, debug information, cross-target differential tests, and future JIT TargetMachine instances.

Move to target-owned side tables keyed by stable semantic IDs.

### B3. Linear identifier lookup — HIGH before broad Linux scale

Repeated linear scans of locals/globals/functions/records/typedefs are correctness-friendly but algorithmically weak for large preprocessed translation units.

Introduce StringId interning first, then C-namespace-aware scoped hash tables. Keep declaration IDs stable so parser/AST interfaces do not depend on hash-table addresses.

### B4. Fat embedded type/source/operand data — MEDIUM/HIGH

Types and source spans are copied by value throughout nodes, and calls/function signatures carry fixed inline arrays. Expanding parameter limits therefore inflates unrelated expressions/functions.

Move toward:

```text
SourceRangeId
StringId
TypeId
(first_operand, operand_count) slices
(first_parameter_type, parameter_count) slices
Arena / phase pools
```

Do this with measurements and regression gates, not as one giant migration.

## C. Extension stress tests / 扩展压力测试

### C1. Can we add a real IR without rewriting the parser? — YES

There is already a seam after frontend normalization and before RV64 layout/codegen. Core IR should enter there. Existing parser/typed-AST semantics remain reusable.

### C2. Can we add -O1/-O2 globally? — NOT CLEANLY YET, but fixable

Direct recursive AST-to-assembly codegen prevents systematic CFG/dataflow optimization. Core IR + PassManager resolves this without changing source parsing.

### C3. Can we support another target? — PARTIALLY

Directory structure permits it, but frontend-owned storage layout and RV64 assumptions in generic type logic must move behind TargetInfo/DataLayout before a second target is clean.

### C4. Can we support C89/C99/C11/C17/C23 + GNU modes? — PARTIALLY

Current syntax grows from real programs, but feature availability is not yet governed by one LanguageOptions object. Add dialect policy before extension coverage becomes too broad.

### C5. Can GNU features be added without scattering checks? — NOT YET

Discovery implementations correctly reject dangerous unknown attributes in several places, but permanent architecture needs thin Attribute/Builtin/Pragma registries plus explicit semantic/lowering consumers.

### C6. Can native assembler/linker be inserted later? — YES if target/object model is shared

Do not make the compiler call a standalone assembler API internally. Instead share instruction encoding, symbols, sections, and relocations beneath both backend object emission and standalone assembler/linker tools.

### C7. Can dynamic linking be added later? — YES if object/linker/runtime policy stays separated

ELF dynamic sections/GOT/PLT/relocations belong to linker/object layers. A runtime loader, if implemented, is a separate component. Existing platform libc/loader remain valid targets.

### C8. Can TinyCC-like in-memory execution be added later? — YES with the proposed boundary

Normal frontend -> IR -> target lowering remains unchanged. A memory object writer + relocation resolver + executable memory manager replaces file-oriented object/link steps.

This is exactly why machine encoding and relocations should not live only inside a textual assembler printer.

### C9. Can DWARF/debug support be added later? — YES only if source mappings survive IR/MIR

No debug-info emitter is needed now. But every lowering step must preserve SourceRange/Decl/Type origin mappings so DWARF can be emitted later without reconstructing lost provenance.

### C10. Can we self-host? — YES, and modularity helps

Self-hosting does not require a TinyCC-style monolith. Keep bootstrap-critical implementation in the chosen C baseline, make generated state deterministic, and replace surrounding external stages independently after compiler self-compilation is stable.

## D. Required migration order / 强制迁移顺序

Order matters because some changes make later ones substantially cheaper.

1. **Freeze/materialize discovery semantics.** No behavior redesign.
2. **Add phase telemetry and structural-size statistics.** Measure before optimizing.
3. **SourceManager + StringId.** Remove duplicated source/name payload.
4. **Scoped SymbolTable.** Remove linear lookup hot paths.
5. **TypeId interning.** Reduce node size and make IR type references stable.
6. **TargetInfo/DataLayout/ABI side tables.** Make frontend target-neutral.
7. **Reusable Declarator + LanguageOptions + extension registries.** Stop syntax-growth fragmentation.
8. **Core IR v0 + verifier.** Initially optimization-empty.
9. **Migrate RV64 function lowering behind Core IR one function/category at a time with differential gates.**
10. **PassManager + measured optimization pipeline.**
11. **Machine IR / vregs / register allocation / encoder.**
12. **Native object emission.**
13. **Standalone assembler/linker/object tools and dynamic support when real milestones demand them.**
14. **Optional memory execution and deeper self-hosting reuse those same lower layers.**

## E. Refactor safety gates / 重构安全门禁

Every structural migration should run four layers of evidence:

```text
focused semantic tests
frozen real projects (tiny-AES/cJSON/Parson/linenoise/SDS/Lua as applicable)
representative Linux translation units
Linux object/relink/runtime gate at the current frozen frontier
```

For representation-only changes, add deterministic AST/IR dumps or object/assembly equivalence where expected. For intentional codegen changes, compare runtime and ABI/object metadata instead of requiring byte identity.

## F. Architecture debt budget / 架构债务预算

A discovery patch is allowed when it answers one real upstream blocker quickly. It becomes unacceptable debt when any of these happens:

- a second feature needs to patch the same semantic area textually;
- an anchor collision requires renaming/rewording unrelated code;
- a patch changes a public data structure used by several later patches;
- a workaround cannot be explained by a language/ABI/object invariant;
- CI compiles generated effective source that reviewers cannot see in the branch;
- the same capability is needed by more than one frozen real project.

At that point the feature must move into the permanent C subsystem before further breadth is added.

## G. Next architecture milestone / 下一架构里程碑

Do **not** stop the Linux probe merely to invent IR features blindly. Continue using Linux to discover semantic pressure, while preparing a consolidation snapshot. Once the currently staged compiler is materialized and frozen, the first permanent architecture implementation should be SourceManager/StringId/SymbolTable/TargetInfo groundwork; then Core IR can be introduced without carrying avoidable frontend representation debt into the mid-end.
