# cJSON external-project status / cJSON 外部项目状态

## Status / 状态

cJSON 1.7.19 is the active second real-project workload after the frozen tiny-AES AES-128 ECB milestone. The unchanged pinned core is used as a reproducible compiler-frontier workload; MiniC does **not** yet claim complete cJSON build/runtime acceptance.

cJSON 1.7.19 是冻结 tiny-AES AES-128 ECB 里程碑之后的第二个真实项目。未修改的固定核心用于可重复的编译器前沿验证；当前仍**不**宣称 MiniC 已完整构建并运行 cJSON。

## Upstream identity / 上游身份

- Repository / 仓库：`DaveGamble/cJSON`
- Release / 版本：`1.7.19` (`v1.7.19`)
- Commit / 提交：`c859b25da02955fef659d658b8f324b5cde87be3`
- License / 许可证：MIT
- Vendored source / 入库源码：`tests/vendor/cjson/upstream/`
- `cJSON.c` blob：`6e4fb0dd369cd905923da515be87ab06db6c1ee0`
- `cJSON.h` blob：`cab5feb427725f8e5c82287f7fe59481b609b9b5`
- `LICENSE` blob：`78deb0406d713ab9730e3c2447be1abdbd70b9a2`

External GCC may preprocess, assemble, link, and provide CRT/libc. MiniC must compile every accepted C function and may not silently delegate unsupported functions to GCC.

外部 GCC 可以承担预处理、汇编、链接和 CRT/libc；MiniC 必须编译验收输入中的所有 C 函数，不得静默把不支持的函数交给 GCC。

## Accepted compiler capabilities / 已越过能力

The unchanged core now crosses LONG semantics, self-referential records, plain `char`, float/double object types, function-pointer record fields, pointer qualifiers, anonymous struct typedefs, zero-initialized static records, direct record-member access, pointer returns, null-pointer constants, RV64D double returns, decimal double literals, same-type double arithmetic, function-scope static fixed arrays, variadic direct declarations and calls, contextual array-to-pointer decay, and narrow string literals.

未修改 cJSON 核心现已越过 LONG、自引用 record、plain `char`、float/double 对象、函数指针字段、pointer qualifier、匿名 struct typedef、静态 record 全零初始化、direct member access、pointer return、null pointer constant、RV64D double return、十进制 double literal、double 四则运算、函数作用域 static 定长数组、variadic direct declaration/call、上下文化 array-to-pointer decay 与窄字符串字面量。

This branch adds bounded pointer equality for `==` / `!=`:

- compatible pointer ↔ pointer comparisons reuse the existing assignment-compatibility model in either direction;
- object pointer ↔ `void *` and qualification-compatible pointer comparisons are accepted;
- pointer ↔ integer constant zero is accepted in either operand order as a null-pointer comparison;
- pointer ↔ nonzero integer remains rejected;
- incompatible pointer identities remain rejected;
- relational pointer comparisons (`< <= > >=`) remain outside this branch;
- the parsed/normalized AST verifier and RV64 backend consume the same accepted shapes;
- RV64 lowering reuses address-bit `xor + seqz/snez`.

本分支新增受控的 pointer equality：兼容 pointer↔pointer、object pointer↔`void *`、qualification-compatible pointer，以及 pointer↔整数常量 0；非零整数、不兼容 pointer、pointer relational comparison 均继续拒绝。parsed/normalized AST verifier 与 RV64 backend 使用同一组允许形状，后端复用地址位 `xor + seqz/snez`。

A permanent `pointer_equality` GCC/MiniC RV64 differential program is registered as the 49th program. It covers `p == 0`, `0 == p`, `p != q`, real object addresses, `int *`↔`const int *`, object pointer↔`void *`, and `(void *)0`. Focused negative fixtures preserve the nonzero-integer, incompatible-pointer, and relational-comparison boundaries.

永久 `pointer_equality` GCC/MiniC RV64 differential 已登记为第 49 个程序，同时 focused negative fixtures 锁住 nonzero integer、不兼容 pointer 与 relational comparison 的边界。

## Current exact frontier / 当前精确前沿

The stable preprocessed line remains:

```c
    if ((string1 == ((void *)0)) || (string2 == ((void *)0)))
```

Discovery Run #868 crossed the first `string1 == ((void *)0)` comparison and produced the next exact diagnostic:

```text
cJSON.i:131:34: error: unexpected character '|'
```

The first new missing capability is therefore the logical-OR token / short-circuit condition path. This branch intentionally does **not** implement `||` or `&&`; they belong to the next scalar-condition semantic cluster.

Run #868 已越过第一个 `string1 == NULL` 比较，并在第一个 `|` 上得到精确诊断。因此下一条真实能力已经进入 logical OR / short-circuit condition；本分支不顺手实现 `||`/`&&`，它们进入下一条 scalar-condition 语义簇。

## Validation / 验证

Discovery Run #868 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, existing focused suites, RV64 focused tests, and frozen tiny-AES. It also proved that unchanged cJSON crosses the previous pointer-equality parser frontier.

Run #868 同时暴露了两个收口缺口：新 `pointer_equality.c` 尚未登记进 permanent differential manifest，且 focused pointer-equality runner 尚未接入 Phase 2；因此不能把 #868 当作 pointer-equality runtime acceptance。Both are now wired, and the branch additionally closes parsed/normalized AST verification plus RV64 lowering. A latest-head clean run is required before merge.

Run #868 通过 source inventory、clang-format 18、Debug、Release `-Werror`、ASan/UBSan、既有 focused suites、RV64 focused 与冻结 tiny-AES，并证明 unchanged cJSON 越过旧 pointer-equality Parser 前沿；但它也暴露了 differential manifest 与 focused gate 尚未接线的问题。当前分支已补齐这两项，并补齐 AST verifier 与 RV64 lowering；合并前必须以最新 Head clean run 为准。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
