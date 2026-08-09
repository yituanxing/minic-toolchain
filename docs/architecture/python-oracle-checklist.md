# Python Oracle Checklist / Python 历史实现参考清单

The historical Python MiniC is an executable behavior and engineering oracle, not a C source template.

## Preserve

- Real upstream software drives feature priority.
- Upstream build systems should increasingly drive MiniC through a compiler wrapper instead of hand-selecting source files forever.
- Capability growth and coverage growth are separate: a version may expand real-project coverage without changing compiler code.
- Every milestone keeps accurate claims, explicit non-claims, exact toolchain/configuration identity, object inventories, hashes, and reproducible commands.
- Linux validation uses an exact GCC/Kbuild baseline, canonical Kbuild `.i` inputs, explicit MiniC object provenance, normal Kbuild relinking, and a clean cold-build acceptance gate.
- Runtime failures are isolated with GCC/MiniC A/B object substitution and binary search before adding compiler workarounds.
- Performance changes are accepted only after fixed real Linux translation units and regression gates remain correct.

## Historical bug classes to freeze as C-era regressions

- aggregate compound literals must never be coerced to integer zero by an unsupported constant evaluator;
- GNU lifetime/control-flow attributes such as `cleanup` must have an explicit semantic/lowering consumer;
- bitfield storage-unit read/modify/write must be distinct from source-level `_Bool` value conversion;
- dead constant branches and side-effect-free code must not retain object relocations to dead symbols;
- section/visibility/weak/alias metadata must survive through frontend symbols to object emission;
- generated Kbuild objects such as `init/version.o` must be accounted for in provenance rather than silently accepted as GCC fallback;
- temporary modpost/bootstrap symbol inputs are discovery aids only and must be absent from final cold acceptance.

## Do not copy

- giant Python modules and broad mutable compiler state;
- target ABI/layout facts embedded in generic type semantics;
- repeated string copying and linear name lookup as the permanent symbol model;
- scattered extension-name checks across parser/codegen;
- multiple inconsistent constant evaluators;
- parser/codegen special cases used as a substitute for a transformable IR;
- ordered source-text patching as the permanent production architecture.

## C rewrite rule

When a real blocker appears:

1. identify the exact unchanged upstream construct;
2. check historical Python behavior and earlier failure records for semantics and pitfalls;
3. decide which current C layer owns the capability;
4. implement the generic capability in the current architecture;
5. freeze it with a focused regression;
6. return immediately to the real project;
7. for Linux runtime failures, reuse the old object A/B and binary-search workflow before deeper instrumentation.

Python tells us what behavior was proven and where old mistakes happened. The C architecture decides where that behavior belongs now.
