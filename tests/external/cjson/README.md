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

The unchanged core now crosses the previously frozen language features, scalar-condition semantics, generalized bounded `for` clauses, and narrow character constants.

本分支新增窄字符常量：Lexer 保留独立 `CHARACTER_CONSTANT` token，Parser 将普通单字符和基本单字符 escape 解码为标准 C `int` rvalue，AST / verifier / RV64 继续复用既有 integer constant 路径。多字符常量以及 octal/hex escape 仍保留为后续边界。

A permanent `character_literals` GCC/MiniC RV64 differential is registered as the 52nd program. It covers `\0`, ordinary ASCII literals, newline/tab, slash/quote/question escapes, and integer-expression use.

永久 `character_literals` GCC/MiniC RV64 differential 已登记为第 52 个程序。

## Current exact frontier / 当前精确前沿

Discovery Run #903 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, existing focused suites, all 52 permanent GCC/MiniC RV64 differential programs including `character_literals`, and frozen tiny-AES.

The unchanged cJSON source crossed:

```c
        if (*string1 == '\0')
```

and reached the function-valued static record initializer:

```c
static internal_hooks global_hooks = { malloc, free, realloc };
```

with exact first diagnostic:

```text
cJSON.i:155:40: error: static pointer initializer must be null
```

The next missing capability is therefore no longer a literal/`sizeof` issue. It is the function-value / static function-pointer initializer cluster: function designators must become pointer values and aggregate static initialization must be able to carry relocatable function addresses. `sizeof` appears later and is intentionally not pulled into this branch before real source reaches it.

Run #903 已证明字符常量在 RV64/QEMU 中通过，并将 unchanged cJSON 推进到函数地址静态初始化。下一条转入 function-value / static initializer 语义簇；本分支不因为原计划名称包含 `sizeof` 就强行提前实现它。

## Validation / 验证

Run #903 passed every compiler/runtime gate except the intentionally stale cJSON frontier. A latest-head clean run with the updated line-155 probe is required before merge.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
