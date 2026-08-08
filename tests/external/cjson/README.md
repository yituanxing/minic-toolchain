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

The unchanged core now crosses the previously frozen language features plus scalar conditions, generalized bounded `for` clauses, character constants, function-valued static relocations, bounded `sizeof`, indirect function calls, runtime function designators, and bounded function-pointer null compatibility.

本分支完成 **function-pointer NULL compatibility**，但没有放宽任意 object pointer / function pointer 互转：

- AST 提供统一的 expression-aware assignment/equality compatibility；
- 裸整数 `0` 与由 pointer cast/bitcast 包裹的确定零值可识别为 null pointer value；
- `(void *)0` 因此可用于 function-pointer 赋值与 `==`/`!=`；
- Parser、AST verifier 和 RV64 backend 共用同一规则；
- local initializer、assignment、return、direct/indirect call fixed arguments 都使用同一 assignment helper；
- 非空 `void *` 到 function pointer 的赋值和比较仍被 focused negative fixtures 拒绝。

A permanent `function_pointer_null` GCC/MiniC RV64 differential is registered as the 57th program. It covers `(void *)0`, bare zero, reversed comparison, restoration to a real function designator, and indirect execution.

永久第 57 个 differential `function_pointer_null` 已在 RV64/QEMU 上通过。

## Current exact frontier / 当前精确前沿

Discovery Runs #941/#944 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, the focused null-boundary tests, all **57** permanent GCC/MiniC RV64 differential programs, and frozen tiny-AES. Run #944 specifically proves the two non-null `void *` negative fixtures reach the intended function-pointer incompatibility boundary.

The unchanged cJSON source crossed all hook-null operations, including:

```c
if (hooks->malloc_fn != NULL)
global_hooks.reallocate = NULL;
```

and continued through `cJSON_New_Item` and `cJSON_Delete`. The new exact first diagnostic is:

```text
cJSON.i:255:5: error: expected compound, if, while, for, break, declaration, expression, return, or '}'
```

The frozen preprocessed source line is:

```c
    double number = 0;
```

This is the first true local `double` object in the unchanged cJSON path. MiniC already supports double type layout, literals, basic double arithmetic, and double return ABI, but does not yet support floating local stack objects and their initialization/load/store semantics.

当前下一能力簇因此是 **local double object / floating local storage**，不是控制流语法。下一分支应建立局部 `double` 的栈布局、初始化、读取和写回，并继续由 unchanged cJSON 暴露后续转换/比较需求。

## Validation / 验证

Run #944 is the discovery run proving the bounded function-pointer-null semantics and the new line-255 frontier. A latest-head clean run with the updated line-255 probe is required before this branch is merged.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
