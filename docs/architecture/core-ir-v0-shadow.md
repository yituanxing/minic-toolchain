# Core IR v0 shadow / Core IR v0 影子路径

## 1. Purpose / 目的

Core IR is a bounded function-body execution seam, not a second semantic AST and not a machine IR.

Core IR 只建立函数体执行语义的边界，不复制一套 Semantic AST，也不提前建立 Machine IR。

The architecture is organized around three ownership boundaries:

```text
Semantic AST / FunctionBody
        │
        │ C/GNU C rules -> execution facts
        ▼
      Core IR
        │
        │ logical values/calls -> calling-convention contract
        ▼
    Target ABI
        │
        │ abstract ABI locations -> concrete machine realization
        ▼
 machine lowering / RV64
```

The reason for each boundary is ownership, not resemblance to GCC or LLVM:

- Semantic AST owns what the C/GNU C program means.
- Core IR owns what must execute after source-language rules have been resolved, while implementation choices that still belong to lower layers remain open.
- TargetABI owns calling-convention classification and abstract argument/return placement.
- Machine lowering owns physical registers, stack addressing, instruction selection and assembly spelling.

DataLayout remains a read-only target-dependent semantic query service that may be consumed on either side of the AST/Core seam. It is not folded into Core IR or TargetABI.

## 2. Why activate the seam now / 为什么现在启用

The direct AST -> RV64 path accumulated target-neutral responsibilities: expression evaluation order, temporary materialization, control-flow construction, cleanup traversal and aggregate execution rules. The backend was therefore beginning to answer both "what must execute?" and "how does RV64 execute it?".

Core IR separates those questions while the shadow migration keeps emitted assembly unchanged.

## 3. v0 execution-shadow contract / v0 执行影子契约

The current legal Core phase is:

```text
MINIC_CORE_PHASE_EXECUTION_SHADOW
```

It currently permits:

- one function-local entry block;
- immutable `CoreValue` results;
- function-local addressable `CoreObject` identities;
- `INTEGER_CONSTANT` and `INTEGER_ADD`;
- target-neutral `PARAMETER` values indexed by source parameter position;
- `OBJECT_ADDRESS`, typed `LOAD`, and side-effect-only `STORE`;
- an explicit `RETURN` terminator;
- copied function symbol/signature information;
- copied `MinicType` values and source spans.

A `CoreObject` is semantic addressable storage, **not** a stack slot. Stack offsets, frame placement and physical registers remain backend decisions. `OBJECT_ADDRESS` turns object identity into a normal typed pointer value.

Instructions may be value-producing or effect-only. `STORE` has no result value. Value-producing instructions define immutable values exactly once, and values must be defined before use. The single block must have exactly one explicit terminator. The verifier freezes these rules rather than relying on lowering code to behave correctly by convention.

`INTEGER_ADD` does not grant an optimizer a signed-no-overflow assumption. Core IR v0 intentionally does not exploit C undefined-overflow rules as an optimization contract.

## 4. Object, memory and volatile boundary / 对象、内存与 volatile 边界

The first memory slice is deliberately small and is driven by real canonical functions such as:

```c
int main(void) {
    int value = 1;
    return value;
}
```

and:

```c
int main(void) {
    volatile int value = 1;
    return value;
}
```

The boundary is:

```text
C local object / qualifier rules
        ↓ frontend resolves
CoreObject identity
        ↓
OBJECT_ADDRESS -> pointer value
        ↓
LOAD / STORE memory effect
```

`volatile` is no longer merely source syntax after crossing the seam. The resulting execution fact is stored on the memory access itself:

```text
load.volatile
store.volatile
```

The verifier requires the operation-level volatile flag to agree with the pointee qualifier carried by the address type. This keeps the source-language rule above Core while preserving the observable memory effect below it.

Current object-memory coverage is intentionally limited to scalar integer locals. Arrays, records, register locals, member/subscript addressing and general pointer-derived addresses remain unsupported. Alias analysis is not introduced by this slice.

## 5. Parameter ingress boundary / 参数入口边界

Function parameters are source-level local objects, but their initial values come from the caller. Core now represents that fact explicitly rather than allowing a parameter object to be loaded before any initial value exists:

```text
abstract parameter #N
        ↓
PARAMETER value
        ↓
parameter CoreObject
        ↓
body LOAD / STORE
```

`PARAMETER` identifies the logical incoming value by parameter index only. It does **not** name `a0`, `fa0`, stack locations, hidden ABI slots or frame offsets. Those choices remain below the Core/ABI boundary.

The current lowering supports ordinary non-const/non-volatile integer parameters. Pointer, aggregate, register-storage, const and volatile parameter ingress remain `UNSUPPORTED` until a real case justifies widening the contract.

## 6. What crosses the AST/Core boundary / AST/Core 边界保留什么

Source-language rules are resolved above the seam. Core IR does not reinterpret C integer promotions, lvalue conversion, GNU syntax or declaration lookup.

Facts cross the seam only while a real downstream consumer still needs them. The current contract preserves:

- resolved value/object types;
- function signature and symbol identity;
- abstract incoming parameter identity;
- object identity and addressability;
- execution order expressed by instruction order;
- explicit memory effects and volatile access semantics;
- explicit return behavior;
- source provenance through the current `MinicSourceSpan` value.

The current `MinicType` and `MinicSourceSpan` representations are reused deliberately. Core IR does not create duplicate type or source-location systems merely to appear independent.

The lowering unit is the **normalized canonical `FunctionBody`**, not an imagined source-level tree. For example, the parser appends a canonical default return at function end and materializes parameters at the beginning of the function-local range. Core lowering absorbs those representation facts at the AST/Core seam instead of leaking them into RV64.

## 7. Lowering result contract / Lowering 结果契约

Core lowering is tri-state:

```text
MINIC_CORE_LOWER_OK
MINIC_CORE_LOWER_UNSUPPORTED
MINIC_CORE_LOWER_ERROR
```

- `OK` means a valid CoreFunction was produced and verified.
- `UNSUPPORTED` means the normalized AST is valid but outside the current bounded Core coverage.
- `ERROR` means an internal representation, lookup, allocation or verifier contract failed.

A non-`OK` lowering never partially replaces the caller's existing CoreFunction. This lets real programs expose coverage gaps without confusing them with compiler failures, while actual Core failures still fail closed.

## 8. Pipeline shadow migration / 编译管线影子迁移

The default production path remains unchanged:

```text
normalized Semantic AST
        ↓
existing RV64 layout/backend
        ↓
assembly
```

The opt-in development shadow runs after normalized AST and FunctionBody validation and before RV64 layout:

```text
normalized Semantic AST
        ↓
FunctionBody ownership validation
        ↓
Core shadow: lower -> verify -> destroy
        ↓
existing RV64 layout/backend
        ↓
assembly
```

The temporary internal environment switch is:

```text
MINIC_CORE_IR unset   -> shadow disabled
MINIC_CORE_IR=shadow  -> lower supported functions; skip UNSUPPORTED
MINIC_CORE_IR=strict  -> fail if any defined function is UNSUPPORTED
```

`strict` exists so CI can prove that the shadow path actually executed; it is not a public compiler API. The switch should be deleted when Core IR becomes the production function-body path.

The pipeline contract currently freezes:

- scalar `return 1 + 2` under strict shadow;
- ordinary local initialization/read under strict shadow;
- volatile local initialization/read under strict shadow;
- ordinary integer parameter ingress/read under strict shadow;
- unsupported subtraction skipped by optional shadow and rejected by strict shadow;
- whenever default and shadow compilation both succeed, their RV64 assembly is byte-identical.

No RV64 code-generation API consumes Core IR yet.

## 9. What is deliberately not designed yet / 现在不提前设计什么

This slice does not create:

- SSA or phi nodes;
- a pass manager;
- alias analysis;
- Machine IR;
- a second type system;
- ABI locations inside Core IR;
- physical register or frame information;
- a complete C undefined-behavior model;
- cleanup, switch, aggregate or inline-asm Core operations.

Those concepts are introduced only when a real lowering slice needs them. The rule remains: resolve a rule where it belongs, preserve the resulting fact while a real consumer needs it, and lower only when the remaining choice belongs to the next layer.

## 10. Next widening criterion / 下一刀

The next widening must come from the smallest real function that exceeds the current parameter/object-memory coverage. Do not broaden addressing, CFG, calls or aggregate semantics merely to fill an opcode list. Each widening must preserve the focused shadow contract, Foundation gates and unchanged real-program behavior before another responsibility moves into Core.
