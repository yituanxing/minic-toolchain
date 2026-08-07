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

The unchanged core now crosses the previously frozen language features plus the integer/pointer scalar-condition cluster and the bounded `for`-clause/update cluster.

本分支继续越过 integer/pointer scalar-condition 之后的 `for` clause/update 语义簇。

The `for` cluster keeps the existing `for` -> `while` lowering but broadens the parser scaffold to accept:

- empty `for` initializer and condition clauses;
- comma-separated prefix/postfix `++` / `--` updates in the for-post clause;
- integer and complete-object-pointer local updates;
- discarded `(void)p++` spelling used by real C;
- a real hosted `int tolower(int)` declaration in the cJSON probe.

This is intentionally transitional: a final general `CommaExpr` / `AssignExpr` AST is **not** introduced until ordinary real-project expression contexts require it.

当前实现继续复用 `for -> while` lowering，并支持 empty clause、for-post 中逗号分隔的前/后缀 `++/--`、integer/完整对象 pointer update，以及 `(void)p++`。这里没有为了当前一行源码提前建立最终 `CommaExpr` / `AssignExpr` 架构。

A permanent `for_clause_updates` GCC/MiniC RV64 differential is registered as the 51st program. It verifies an empty initializer plus ordered `(void)pointer++, index++` updates and checks the resulting array contents, integer counter, and pointer position under QEMU.

永久 `for_clause_updates` GCC/MiniC RV64 differential 已登记为第 51 个程序，并在 QEMU 中验证 empty initializer、pointer/integer comma update 的顺序及结果。

## Current exact frontier / 当前精确前沿

Discovery Run #898 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, existing focused suites, all 51 permanent GCC/MiniC RV64 differential programs including `for_clause_updates`, and frozen tiny-AES.

The unchanged cJSON source crossed:

```c
    for(; tolower(*string1) == tolower(*string2); (void)string1++, string2++)
```

and reached:

```c
        if (*string1 == '\0')
```

with the exact first diagnostic:

```text
cJSON.i:142:25: error: unexpected character '''
```

The next missing capability is therefore a narrow character literal. It is outside this `for`-clause cluster and will be handled on the next semantic line rather than widening this PR.

Run #898 已证明 `for` clause/update 簇真实跨过并把 unchanged cJSON 推进到 `*string1 == '\0'`。下一条首个缺失能力是 character literal，不继续塞入当前分支。

## Validation / 验证

Run #898 passed all compiler gates except the intentionally stale cJSON frontier and proved `for_clause_updates` under RV64/QEMU. A latest-head clean run with the updated character-literal frontier is required before merge.

Run #898 除故意保持旧值的 cJSON frontier 外全部通过；最新 Head 仍需用更新后的 character-literal probe 完整 clean 验证后才可合并。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
