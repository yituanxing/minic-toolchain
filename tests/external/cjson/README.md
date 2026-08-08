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

The unchanged core now crosses the previously frozen language features plus scalar conditions, generalized bounded `for` clauses, character constants, function-valued static relocations, bounded `sizeof`, and generic non-variadic calls through function-pointer expressions.

本分支完成通用 **callee-expression / indirect function call**：

- 继续复用单一 `MINIC_EXPRESSION_CALL`；
- direct call 保留 `function_id`，indirect call 使用 callee expression；
- postfix `()` 可作用于 pointer-to-function expression，包括 record member；
- 参数类型从既有 `MinicFunctionType` 验证；
- normalization 与 verifier 都把 indirect callee 作为真实表达式边处理；
- RV64 在求值参数之前保存 callee 地址，参数恢复到 `a0..a7` 后重新加载到 `t0`，最后执行 `jalr ra, t0, 0`；
- direct / indirect 共用返回值规范化路径。

这一结构与冻结 Python 编译器的通用 `Call(name, args, callee)` 模型一致，没有引入 cJSON hook 特判。

A permanent `indirect_function_calls` GCC/MiniC RV64 differential is registered as the 55th program. It covers static record function-pointer initialization, ordinary indirect calls, and nested indirect calls so the outer callee must survive evaluation of an inner call argument.

永久第 55 个 differential `indirect_function_calls` 已在 RV64/QEMU 上验证，包括 nested indirect call 的 callee 生命周期。

## Current exact frontier / 当前精确前沿

Discovery Run #933 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, focused suites, all **55** permanent GCC/MiniC RV64 differential programs including `indirect_function_calls`, and frozen tiny-AES.

The unchanged cJSON source crossed:

```c
copy = (unsigned char*)hooks->allocate(length);
```

and continued into `cJSON_InitHooks`, where it reached:

```c
global_hooks.allocate = malloc;
```

with exact first diagnostic:

```text
cJSON.i:187:39: error: use of undeclared local
```

This is a new capability boundary. Static function-address relocations already exist, but a bare function designator such as `malloc` is not yet accepted as a general runtime expression value on the right-hand side of an assignment. The next branch should therefore add generic runtime function-designator-to-pointer semantics and RV64 symbol-address materialization, rather than extending the indirect-call PR.

当前下一能力簇是通用 **runtime function designator value**：让函数名在普通表达式上下文中衰变/形成函数指针值，并可参与赋值；这与本分支已经完成的“通过函数指针调用”是相邻但独立的语义层。

## Validation / 验证

Run #933 is the discovery run proving the 55th differential and the new line-187 frontier. A latest-head clean run with the updated line-187 probe is required before this branch is merged.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
