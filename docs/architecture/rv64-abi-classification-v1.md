# RV64 ABI classification and placement v1 / RV64 ABI 分类与参数位置 v1

## 1. Why this slice comes before FrameLayout extraction / 为什么先做 ABI，而不是先拆 FrameLayout

After the FunctionBody ownership slice landed, the next global reread showed that `DataLayout` itself was already a relatively clean read-only C object/type query service. The larger ownership problem was lower in the RV64 backend.

Function frame construction, callee entry, caller argument lowering and return lowering were each interpreting parts of the same calling-convention rules independently. In particular, integer/pointer arguments, fixed floating arguments, small integer-only records and stack overflow were being classified in several places.

如果先把 local/frame placement 从 AST 拆到新的 side state，却继续让这些模块各自解释 ABI，就会把重复规则固化进新的结构。因此这一刀先建立 TargetABI 的 canonical classification/location seam，再考虑 FrameLayout ownership。

## 2. Scope / 范围

This v1 introduces checked-in `src/target/riscv64/abi.[ch]` with two distinct concepts:

```text
C/RV64 value type
    ↓
ABI value classification
    ↓
abstract argument location
    ↓
backend register spelling / stack addressing
```

The ABI layer owns classification and abstract slots. It does **not** emit assembly and does not contain strings such as `a0`, `a1`, `fa0` or `sp`.

The public internal model is deliberately small:

```c
MinicRiscv64AbiValue
MinicRiscv64AbiCursor
MinicRiscv64AbiArgumentLocation

minic_riscv64_abi_classify_value(...)
minic_riscv64_abi_place_argument(...)
```

`MinicRiscv64AbiArgumentLocation` describes integer-register, floating-register and stack-slot ranges. The RV64 emitter remains responsible for mapping those abstract locations to physical assembly registers and addresses.

## 3. Formal v1 behavior / formal v1 当前行为

The checked-in classifier intentionally freezes the behavior already supported by the formal Foundation compiler rather than importing every discovery-only Linux capability in the same structural PR.

Current formal classifications are:

- `void` -> `VOID`;
- integer and pointer values -> `INTEGER`;
- `float` / `double` -> `FLOAT`;
- integer-only records of exactly 8 or 16 bytes -> `AGGREGATE` with one or two integer slots;
- unsupported record shapes fail closed.

Argument placement currently freezes the existing compiler behavior:

- named/fixed floating arguments use abstract floating-register slots;
- integer/pointer arguments use integer-register slots and then stack slots;
- supported small integer aggregates may split between the final integer register and the stack;
- non-fixed floating arguments follow the integer convention after the frontend's variadic conversions;
- unsupported cases leave the placement cursor unchanged and fail closed.

This is a bounded ownership migration, not a claim that v1 is the complete RISC-V psABI implementation.

## 4. Canonical consumers already migrated / 已迁移的 canonical consumer

The old `minic_riscv64_integer_aggregate_abi()` helper remains as a compatibility-shaped backend helper, but it no longer owns its own recursive record classifier. It delegates to `minic_riscv64_abi_classify_value()`.

That means existing caller, callee and return paths that still call the helper already consume the same aggregate classification owner.

`minic_riscv64_frame_layout()` also no longer independently counts parameter kinds. It walks fixed parameters through one `MinicRiscv64AbiCursor` and `minic_riscv64_abi_place_argument()`, then derives the current varargs register-save size from that abstract placement state.

The physical caller/callee move loops are intentionally not migrated in this v1. They still own assembly-level actions such as selecting `aN`/`faN`, loading stack arguments and storing incoming values. Moving those loops is a separate consumer-migration slice and should be justified after rereading the new formal head.

## 5. What this slice does not do / 本切片不做什么

This PR does not:

- move `MinicLocal.storage_offset` or `MinicFunction.local_storage_size` out of the semantic Program representation;
- introduce a new FrameLayout side table;
- implement the discovery-only zero-sized aggregate, sub-XLEN aggregate or >16-byte indirect-by-value behavior in formal product source;
- implement the complete hard-float aggregate psABI;
- move physical register names into TargetABI;
- add Core IR or Machine IR;
- change the direct AST -> textual RV64 assembly pipeline.

These are separate capability or ownership questions and must not be hidden inside an apparently small classifier refactor.

## 6. Focused contract / 聚焦契约

`tests/target/riscv64/abi_test.c` freezes both successful and fail-closed behavior.

It covers:

- integer, pointer, floating and void classification;
- an integer-only 16-byte record as a two-slot aggregate;
- currently unsupported 4-byte and floating-member records;
- integer-register exhaustion and stack placement;
- an aggregate split across the last integer register and the stack;
- named floating-register placement;
- variadic/non-fixed floating placement through the integer convention;
- transactional failure when FP slots or an unsupported value are encountered.

The test is permanently part of the normal Makefile gate:

```text
make check-fast
    -> check-rv64-abi
```

It is therefore not dependent on a temporary feature-branch workflow after this slice lands.

## 7. Validation evidence / 验收证据

The product branch was validated before Linux pressure with a clean checked-in source list and no source materializer.

Run `31766160926` on head `974137598d73078206470c0659ace077ec8c09ac` passed:

- dedicated ABI focused contract;
- production source inventory;
- clang-format check;
- release `-Werror` + `check-fast`, including the permanent `check-rv64-abi` target;
- sanitizer AST contracts;
- frozen Foundation focused semantics;
- the official full compiler gate, including existing RV64 and unchanged real-program regressions.

## 8. Linux pressure revalidation / Linux 压力回归

The frozen Linux pressure check must preserve previously discovered semantics rather than forcing the formal branch to pretend those capabilities do not exist.

A runner-only revalidation therefore used this structure:

```text
proven discovery semantic tail
        +
current formal Foundation architecture
        +
formal ABI API / exact migrated consumer shapes
        +
discovery-only broader ABI implementation behind the same seam
        ↓
unchanged Linux 6.6.143 init/main.i
```

The runner compatibility implementation retained the already-proven discovery semantics for zero-sized aggregates, sub-XLEN integer aggregates and indirect large records, while exposing the new formal `AbiValue / AbiCursor / ArgumentLocation` interface. The exact formal versions of `minic_riscv64_integer_aggregate_abi()` and `minic_riscv64_frame_layout()` were overlaid into that workspace.

This is intentionally **not** a second product ABI owner. The compatibility implementation exists only in the staging runner so Linux can test whether the new interface can carry the broader proven semantics without merging discovery code into the formal branch.

Run `31765991026` hard-asserted compiler status and non-empty assembly output and completed with:

```text
ABI_FORMAL_CONSUMERS_OVERLAID=2
cached_tu_status=0
FULL_TU_PASS lines=90928
```

No Linux progression beyond the frozen `main.i` translation unit was performed.

## 9. Architectural consequence / 架构结论

The important result is not merely that another `abi.c` file exists. The ownership direction is now explicit:

```text
DataLayout
    ↓
TargetABI classification / abstract locations
    ↓
Frame / caller / callee / return consumers
    ↓
RV64 physical register and assembly emission
```

Discovery has also demonstrated that the seam can be widened to zero/sub-XLEN/indirect aggregate behavior without moving register strings or stack-offset emission upward.

After this slice lands, reread the new formal Foundation globally. The next step may be physical caller/callee consumer migration, FrameLayout side-state extraction, or another ownership problem exposed by the reread; it is not predetermined by a checklist.
