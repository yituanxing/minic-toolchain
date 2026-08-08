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

The unchanged core now crosses the previously frozen language features plus scalar conditions, generalized bounded `for` clauses, character constants, function-valued static relocations, bounded `sizeof`, generic non-variadic calls through function-pointer expressions, and generic runtime function designators.

本分支完成通用 **runtime function designator value**：

- 新增独立 `MINIC_EXPRESSION_FUNCTION`，不把函数地址伪装成整数或普通全局对象；
- 局部变量/参数和对象继续优先遮蔽同名函数，只有未遮蔽的已声明函数在普通表达式上下文形成函数指针 rvalue；
- 从函数声明签名构造/复用 `MinicFunctionType`，再形成 pointer-to-function；
- direct `function(...)` 路径保持不变；
- verifier 校验 designator 的函数指针类型与声明返回值/参数逐项一致；
- normalization 将 designator 作为无 child 的叶节点；
- RV64 通过 `la a0, <symbol>` 形成可重定位运行时函数地址；
- 当前函数类型模型尚无 variadic 标记，因此 variadic function designator 保留为显式后续边界。

这一规则与冻结 Python 编译器一致：local/param shadowing 优先，未遮蔽 function symbol 在表达式中直接 lower 为 symbol address。

A permanent `runtime_function_designators` GCC/MiniC RV64 differential is registered as the 56th program. It covers static relocation baseline, runtime function-pointer assignment, equality in both operand orders, indirect calls after reassignment, and local shadowing.

永久第 56 个 differential `runtime_function_designators` 已在 RV64/QEMU 上通过。

## Current exact frontier / 当前精确前沿

Discovery Run #937 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, focused suites, all **56** permanent GCC/MiniC RV64 differential programs including `runtime_function_designators`, and frozen tiny-AES.

The unchanged cJSON source crossed runtime assignments such as:

```c
global_hooks.allocate = malloc;
global_hooks.deallocate = free;
global_hooks.reallocate = realloc;
```

and reached the function-pointer null comparison:

```c
if (hooks->malloc_fn != NULL)
```

With the probe's C11 `NULL` definition, the preprocessed form is:

```c
if (hooks->malloc_fn != ((void *)0))
```

with exact first diagnostic:

```text
cJSON.i:193:40: error: binary operator requires int operands
```

This is a new independent capability boundary. The existing pointer-equality path permits compatible pointers and integer-zero null pointer constants, while `void *` compatibility deliberately excludes function pointers. cJSON therefore exposes the bounded function-pointer / `(void *)0` null compatibility cluster next.

当前下一能力簇是 **function-pointer NULL compatibility**，不是 runtime function designator 的尾巴。后续分支应明确限定 null semantics，而不是放宽任意 object-pointer / function-pointer 互转。

## Validation / 验证

Run #937 is the discovery run proving the 56th differential and the new line-193 frontier. A latest-head clean run with the updated line-193 probe is required before this branch is merged.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
