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

The unchanged core now crosses the previously frozen language features plus scalar conditions, generalized bounded `for` clauses, character constants, function-valued static relocations, bounded `sizeof`, indirect function calls, runtime function designators, bounded function-pointer null compatibility, local `double` storage, integer-to-double numeric conversion, and bounded `switch/case/default` control flow.

本分支完成 **switch / case / default control flow**：

- `switch`、`case`、`default` 与 `:` 已进入正式 token 模型；
- switch selector 当前要求整数表达式；
- case label 当前接受已建模的单个整数/字符常量，重复 case 与重复 default 会被拒绝；
- AST 使用独立 `SWITCH / CASE / DEFAULT` statement kind；
- switch body 保持源码顺序，连续 case 与 fallthrough 不被改写成伪 if/else；
- RV64 selector 只求值一次，然后 dispatch 到稳定的 case/default label；
- `break` 不再绑定 `.Lwhile_end_*`，而是退出最内层 loop 或 switch；
- nested switch 的 case/default 不会被外层 dispatch 收集。

A permanent `switch_control_flow` GCC/MiniC RV64 differential is registered as the 60th program. It covers selector single evaluation, adjacent cases, fallthrough, default dispatch, nested switch, and both switch-inside-loop and loop-inside-switch break targeting.

永久第 60 个 differential `switch_control_flow` 已在 RV64/QEMU 上与 GCC reference 对照通过。

## Current exact frontier / 当前精确前沿

Discovery Run #976 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, all focused switch tests, all **60** permanent GCC/MiniC RV64 differential programs including `switch_control_flow`, RV64 focused tests, and frozen tiny-AES. Its only failure was the intentionally stale cJSON frontier gate.

Run #976 proves unchanged cJSON crossed the complete character-classification switch in `parse_number`, including adjacent character cases, fallthrough-compatible ordered labels, default dispatch, and `break`.

The new exact first diagnostic is:

```text
cJSON.i:284:37: error: expected ';' after expression
```

at:

```c
                number_string_length++;
```

This is no longer switch control flow. MiniC already has bounded postfix `++/--` support in the special `for` update path, but ordinary postfix update expressions are not yet represented as general expression statements.

当前下一能力簇因此是 **general postfix update expression semantics**，起点是普通语句中的 `number_string_length++`。它应复用已有 single-evaluation lvalue/update 经验，而不是把 `for` 专用 parser 继续复制到普通语句；后续 unchanged cJSON 再决定是否自然扩展到更一般的 update/assignment-expression 语义。

## Validation / 验证

Run #976 is the discovery run proving the 60th differential and the new line-284 postfix-update frontier. A latest-head clean run with the updated line-284 cJSON probe is required before this branch is marked Ready and Squash merged.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
