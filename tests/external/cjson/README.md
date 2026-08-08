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

The unchanged core now crosses the previously frozen language features plus scalar conditions, generalized bounded `for` clauses, character constants, function-valued static relocations, and bounded `sizeof` semantics.

本分支完成 `sizeof` / no-decay 语义簇：

- Lexer 将 `sizeof` 识别为独立关键字；
- AST 保留独立 `MINIC_EXPRESSION_SIZEOF`，记录被测类型而不是把 operand 作为运行时 child；
- `sizeof(type-name)` 与 `sizeof unary-expression` 均受支持；
- operand 在 array-to-pointer decay 之前保留真实对象类型；
- local array 的既有 `element_count` 表示会在 `sizeof` 上下文重建为数组类型；
- verifier 要求 bounded complete object type；
- RV64 backend 通过 target layout 生成常量，不生成 operand 的运行时代码。

这一结构与冻结 Python 编译器的成熟 `SizeOf(expr|type)` 模型一致：`sizeof` operand 是 unevaluated context，target size 在后续 lowering/layout 层确定，而不是在 Parser 中写死。

A permanent `sizeof_semantics` GCC/MiniC RV64 differential is registered as the 54th program. It covers string literals, primitive integer/floating types, pointers, records, local arrays, and proves non-evaluation with `sizeof(bump(&side_effect))`.

永久 `sizeof_semantics` differential 已登记为第 54 个 RV64/QEMU 程序，并验证 operand 不求值。

## Current exact frontier / 当前精确前沿

Discovery Run #920 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, focused suites, all **54** permanent GCC/MiniC RV64 differential programs including `sizeof_semantics`, and frozen tiny-AES.

The unchanged cJSON source crossed:

```c
length = strlen((const char*)string) + sizeof("");
```

and reached the function-pointer call:

```c
copy = (unsigned char*)hooks->allocate(length);
```

with exact first diagnostic:

```text
cJSON.i:175:43: error: expected ';'
```

The member expression `hooks->allocate` already has the declared pointer-to-function type. The next missing capability is therefore generic postfix call on a function-pointer expression / indirect call lowering, not another cJSON-specific declaration or hook special case.

当前下一能力簇是通用 **callee-expression / indirect function call**：postfix `()` 应接受函数指针表达式，调用节点应统一 direct/indirect callee，RV64 参数布置继续复用既有路径，indirect 分支最终使用 `jalr`。

## Validation / 验证

Run #920 is the discovery run proving the 54th differential and the new line-175 frontier. A latest-head clean run with the updated line-175 probe is required before this branch is merged.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
