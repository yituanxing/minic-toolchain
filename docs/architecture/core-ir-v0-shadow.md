# Core IR v0 shadow / Core IR v0 影子路径

## 1. Purpose / 目的

Core IR starts as a bounded function-body execution seam, not as a second semantic AST and not as a machine IR.

Core IR 首先只建立函数体执行语义的边界，不复制一套 Semantic AST，也不提前建立 Machine IR。

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

The direct AST -> RV64 path has accumulated target-neutral responsibilities: expression evaluation order, temporary materialization, control-flow construction, cleanup traversal and aggregate execution rules. The backend is therefore beginning to answer both "what must execute?" and "how does RV64 execute it?".

Core IR separates those questions while the shadow migration keeps emitted assembly unchanged.

## 3. v0 scalar-shadow contract / v0 标量影子契约

The first legal Core phase is deliberately tiny:

```text
MINIC_CORE_PHASE_SCALAR_SHADOW
```

It currently permits:

- one function-local entry block;
- immutable `CoreValue` results;
- `INTEGER_CONSTANT`;
- `INTEGER_ADD`;
- an explicit `RETURN` terminator;
- copied function symbol/signature information;
- copied `MinicType` values and source spans.

Every instruction in this phase produces exactly one value. Values must be defined before use. The single block must have exactly one explicit terminator. The verifier freezes these rules rather than relying on lowering code to behave correctly by convention.

`INTEGER_ADD` carries the typed execution result but does not grant an optimizer a signed-no-overflow assumption. Core IR v0 intentionally does not exploit C undefined-overflow rules as an optimization contract.

## 4. What crosses the AST/Core boundary / AST/Core 边界保留什么

Source-language rules are resolved above the seam. Core IR does not reinterpret C integer promotions, lvalue conversion, GNU syntax or declaration lookup.

Facts are preserved only when they still have a real downstream consumer. In this first slice that means:

- the resolved value type;
- function signature and symbol identity;
- execution order expressed by instruction order;
- explicit return behavior;
- source provenance through the current `MinicSourceSpan` value.

The current `MinicType` and `MinicSourceSpan` representations are reused deliberately. Core IR does not create duplicate type or source-location systems merely to appear independent.

The lowering unit is the **normalized canonical `FunctionBody`**, not an imagined source-level tree. For example, the parser currently appends a canonical default return at function end. The scalar lowering accepts the first reachable supported return and only tolerates trailing return statements as an unreachable canonical tail; any other trailing statement remains unsupported. Parser representation details are absorbed at the AST/Core seam rather than leaked into RV64.

## 5. Lowering result contract / Lowering 结果契约

Core lowering is tri-state:

```text
MINIC_CORE_LOWER_OK
MINIC_CORE_LOWER_UNSUPPORTED
MINIC_CORE_LOWER_ERROR
```

- `OK` means a valid CoreFunction was produced and verified.
- `UNSUPPORTED` means the normalized AST is valid but outside the current bounded Core coverage.
- `ERROR` means an internal representation, lookup, allocation or verifier contract failed.

A non-`OK` lowering never partially replaces the caller's existing CoreFunction. This distinction is required for a real shadow migration: large real programs may contain unsupported functions without turning expected coverage gaps into compiler errors, while actual Core failures must still fail closed.

## 6. Pipeline shadow migration / 编译管线影子迁移

The default production path remains unchanged:

```text
normalized Semantic AST
        ↓
existing RV64 layout/backend
        ↓
assembly
```

The opt-in development shadow now runs inside the real compiler after normalized AST and FunctionBody validation and before RV64 layout:

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

An invalid value fails closed. `strict` exists so CI can prove that the shadow path actually executed; it is not a public compiler API. The environment switch is migration plumbing and should be deleted when Core IR becomes the production function-body path.

The pipeline contract freezes two important properties:

- a supported `return 1 + 2` function succeeds under `strict`;
- an unsupported `return 1 - 2` function is skipped under `shadow` and rejected under `strict`;
- whenever default and shadow compilation both succeed, their RV64 assembly is byte-identical.

No RV64 code-generation API consumes Core IR yet.

## 7. Memory is intentionally not in this slice / 本切片故意不加入内存操作

v0 does **not** introduce `LOAD`, `STORE`, object addresses or pointer-derived memory operations yet.

The first memory slice must establish the minimum object/address/volatile contract at the same time. Adding primitive load/store operations before deciding what object identity, addressability and volatile effects mean would freeze the wrong boundary merely for opcode minimalism.

This is a bounded deferral, not a plan for a large memory framework.

## 8. What is deliberately not designed yet / 现在不提前设计什么

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

Those concepts are introduced only when a real lowering slice needs them. Their eventual ownership must still follow the same rule: resolve a rule where it belongs, preserve the resulting fact while a real consumer needs it, and lower only when the remaining choice belongs to the next layer.

## 9. Next widening criterion / 下一刀

The next Core IR widening should be driven by the smallest real function that requires local object access. Before the first `LOAD` or `STORE` lands, freeze only the object/address/volatile semantics necessary for that function and re-run the same focused + Foundation + unchanged-real-program gates.
