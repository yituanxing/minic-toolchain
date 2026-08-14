# RV64 callee ABI location consumer v1 / RV64 callee ABI 位置消费 v1

## 1. Why this slice exists / 为什么需要这一刀

The RV64 ABI classification v1 slice established one canonical owner for value classification and abstract argument placement. After that slice landed, a global reread showed that callee entry still maintained its own physical placement cursors:

```text
integer_register_index
floating_register_index
stack_parameter_index
```

That meant classification ownership had converged, but the callee still independently reconstructed argument locations instead of consuming `MinicRiscv64AbiArgumentLocation`.

这一刀只消除这个剩余的位置 ownership 重复，不扩大 ABI 能力，也不迁移 frame/local 存储。

## 2. New callee flow / 新的 callee 流程

Callee parameter materialization now follows one cursor:

```text
Function parameter type
        ↓
MinicRiscv64AbiCursor
        ↓
minic_riscv64_abi_place_argument(...)
        ↓
MinicRiscv64AbiArgumentLocation
        ↓
physical move/store emission
```

The ABI layer owns which abstract integer-register, floating-register and stack slots belong to each formal parameter. `codegen_function.c` still owns the machine action needed to materialize that location into the function-local object.

This preserves the intended boundary:

```text
TargetABI: classification + abstract locations
RV64 emitter: aN/faN spelling + stack addressing + stores/copies
```

No physical register strings were moved into `abi.c`.

## 3. Formal v1 behavior / formal v1 行为

For the formal Foundation compiler, the callee consumes the same bounded ABI capability already frozen by `rv64-abi-classification-v1`:

- fixed `FLOAT` location -> `faN`, bit-moved to an integer temporary and stored into the local parameter object;
- `INTEGER` location -> one integer-register slot or one stack slot;
- supported `AGGREGATE` -> one or two integer-convention slots, including a split between the final integer register and the stack;
- invalid or unsupported location shapes fail closed.

The callee no longer owns independent integer/floating/stack counters.

## 4. What remains deliberately below the seam / 仍留在 emitter 的内容

`codegen_function.c` still owns:

- mapping abstract integer slot N to `aN`;
- mapping abstract floating slot N to `faN`;
- calculating the incoming stack address from `frame_size + stack_slot * 8`;
- moving values into function-local storage;
- storing aggregate chunks;
- prologue/epilogue and varargs register-save emission.

Those are backend placement/emission operations, not ABI classification facts.

## 5. Non-goals / 非目标

This slice does **not**:

- migrate caller call lowering;
- remove `MinicLocal.storage_offset` or `MinicFunction.local_storage_size` from the semantic Program;
- create a new FrameLayout side table;
- implement additional formal psABI capabilities;
- import Linux discovery-only zero-size, sub-XLEN or >16-byte indirect record semantics into product source;
- change record-return semantic lowering;
- add Core IR or Machine IR.

Caller lowering remains a larger later candidate because it currently performs both a placement-count pass and an emission/staging pass. It should not be mixed into this small callee consumer migration merely for symmetry.

## 6. Discovery-only extended ABI pressure / discovery 扩展 ABI 压力

The frozen Linux semantic tail already proved capabilities intentionally wider than formal v1:

- zero-sized aggregate parameters (`IGNORE`);
- sub-XLEN integer aggregates;
- >16-byte by-value record parameters passed indirectly, with caller independent copy and callee local copy.

Linux pressure therefore used a runner-only extended `AbiCursor/ArgumentLocation` adapter rather than widening formal product source inside this refactor.

The extended callee kept discovery semantics while consuming abstract locations:

```text
IGNORE
  -> consumes no location

INDIRECT
  -> one integer-convention location
  -> receive copy pointer from register/stack
  -> byte-copy into callee-local parameter object

AGGREGATE
  -> consume location slot ranges
  -> store each proven sub-XLEN/XLEN chunk locally
```

This validates that the location seam can carry the broader already-proven semantics. The adapter is validation infrastructure only and is not a second product ABI owner.

## 7. Validation evidence / 验收证据

### Candidate validation

The candidate was first staged through a uniquely anchored materializer so the structural rewrite could be tested before becoming canonical source.

Run `31766616093` passed:

- release `-Werror + check-fast`;
- sanitizer AST contracts;
- frozen Foundation focused semantics;
- the official full compiler gate.

### Frozen Linux pressure

An isolated staging branch started from the proven Linux discovery product anchor and overlaid the formal FunctionBody architecture, the already-proven semantic tail, the formal ABI consumer shapes, and the extended location-driven callee.

Run `31766930766` completed with hard assertions:

```text
ABI_FORMAL_CONSUMERS_OVERLAID=2
CALLEE_ABI_LOCATION_EXTENDED=1
cached_tu_status=0
FULL_TU_PASS lines=90928
```

The generated `main.s` was required to be non-empty. No Linux progression beyond the frozen `init/main.i` translation unit was performed.

### Clean checked-in source

After the candidate passed host/full/Linux, the exact materialized and clang-formatted `codegen_function.c` was folded into the feature branch and the one-shot materializer was removed.

Run `31767052392` then validated the clean checked-in shape directly, with no source materialization:

- canonical callee location shape check;
- production source inventory;
- clang-format;
- release `-Werror + check-fast`;
- sanitizer AST contracts;
- frozen Foundation focused semantics;
- official full compiler gate.

Both host and full jobs completed successfully.

## 8. Architectural consequence / 架构结论

The callee no longer decides where a parameter lives. It asks TargetABI for an abstract location and only emits the machine operations required to consume that location.

This leaves the next global reread with three distinct remaining pressures rather than one mixed ABI problem:

1. caller call lowering still reconstructs placement in two passes;
2. frame/local placement is still written into semantic AST fields;
3. record-value semantic lowering still has syntax/value-shape branches.

The next slice must be chosen from the new formal head by evidence, not by automatically continuing the ABI sequence.
