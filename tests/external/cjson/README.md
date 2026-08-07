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

The unchanged core now crosses LONG semantics, self-referential records, plain `char`, float/double object types, function-pointer record fields, pointer qualifiers, anonymous struct typedefs, zero-initialized static records, direct record-member access, pointer returns, null-pointer constants, RV64D double returns, decimal double literals, same-type double arithmetic, function-scope static fixed arrays, variadic direct declarations and calls, contextual array-to-pointer decay, narrow string literals, pointer equality, and the integer/pointer scalar-condition cluster.

未修改 cJSON 核心现已越过 LONG、自引用 record、plain `char`、float/double 对象、函数指针字段、pointer qualifier、匿名 struct typedef、静态 record 全零初始化、direct member access、pointer return、null pointer constant、RV64D double return、十进制 double literal、double 四则运算、函数作用域 static 定长数组、variadic direct declaration/call、上下文化 array-to-pointer decay、窄字符串字面量、pointer equality，以及 integer/pointer scalar-condition 语义簇。

The scalar-condition cluster includes:

- lexical and precedence support for `&&` and `||`;
- true short-circuit lowering on RV64, so the RHS is skipped when required;
- integer and pointer truth conditions for `if`, `while`, and `for` conditions;
- logical `!` for integer and pointer operands;
- parsed/normalized AST verification for the same accepted shapes;
- no floating truth conversion in this cluster.

scalar-condition 语义簇包括 `&&` / `||`、RV64 真短路、integer/pointer 条件值、integer/pointer 的 `!`，以及一致的 parsed/normalized AST verifier；本簇不包含 floating truth conversion。

A permanent `scalar_conditions` GCC/MiniC RV64 differential program is registered as the 50th program. It verifies short-circuit side effects with a counter, pointer truthiness, `!pointer`, nested `&&`/`||`, and `&&` precedence over `||`.

永久 `scalar_conditions` GCC/MiniC RV64 differential 已登记为第 50 个程序。它通过计数器副作用验证 RHS 是否真的被跳过，同时覆盖 pointer truth、`!pointer`、嵌套 `&&`/`||` 与优先级。

## Current exact frontier / 当前精确前沿

The previous cJSON frontier at line 131:

```c
    if ((string1 == ((void *)0)) || (string2 == ((void *)0)))
```

is now crossed. Discovery Run #884 passed the 50th scalar-condition differential under QEMU and advanced the unchanged cJSON source to line 139:

```c
    for(; tolower(*string1) == tolower(*string2); (void)string1++, string2++)
```

The new exact first diagnostic is:

```text
cJSON.i:139:9: error: for initializer requires an assignment
```

The next missing capability is therefore the broader `for`-clause grammar, beginning with an empty initializer. The postfix `++` and comma expression later on the same source line are visible downstream syntax, but they have **not** yet been proven to be the first blocker and are not claimed here.

旧 line 131 `||` 前沿已经越过。Run #884 把 unchanged cJSON 推进到 line 139，新的首个诊断是 `for initializer requires an assignment`。因此下一条能力簇从更一般的 `for` clause grammar、首先是 empty initializer 开始。同一源码行后面的 postfix `++` 和 comma expression 只是已知下游语法，目前不能提前宣称它们是 first blocker。

## Validation / 验证

Discovery Run #884 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, existing focused suites, RV64 focused tests, 50 permanent GCC/MiniC differential programs including `scalar_conditions`, and frozen tiny-AES. The only failure was the intentionally stale cJSON frontier, which exposed line 139 as the next exact boundary.

Run #884 通过 source inventory、clang-format 18、Debug、Release `-Werror`、ASan/UBSan、既有 focused suites、RV64 focused、50 个永久 GCC/MiniC differential（含 `scalar_conditions`）与冻结 tiny-AES。唯一失败是故意保持旧值的 cJSON frontier，并由此暴露 line 139 新边界。

A latest-head clean GitHub Actions run with the updated line-139 probe is required before merge.

合并前仍要求以更新后的 line-139 probe 跑一次 latest-head clean GitHub Actions。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
