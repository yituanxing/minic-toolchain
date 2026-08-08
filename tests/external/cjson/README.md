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

The unchanged core now crosses the previously frozen language features plus scalar conditions, generalized bounded `for` clauses, character constants, function-valued static relocations, bounded `sizeof`, indirect function calls, runtime function designators, bounded function-pointer null compatibility, local `double` storage, and integer-to-double numeric conversion.

本分支完成 **integer → double numeric conversion**，不是简单放宽类型兼容：

- source `(double)integer` cast 现在合法；
- local initialization、普通赋值、return 语境在目标为 `double`、源为整数时插入真实 parsed `CAST`；
- normalization 将该 cast 降成独立 `MINIC_EXPRESSION_CONVERSION`，不把整数 bit pattern 当成 double；
- RV64 按源整数 rank/sign 使用 `fcvt.d.w`、`fcvt.d.wu`、`fcvt.d.l`、`fcvt.d.lu`；
- 转换后的 binary64 bits 通过 `fmv.x.d` 回到 C 后端既有的 `a0` raw-bits 表示；
- `double → integer` 和 `integer → float` 仍由 focused negative gate 明确拒绝；
- double function-call argument ABI 尚未扩大，因为它需要独立的 `fa0..fa7` ABI 能力。

A permanent `integer_to_double_conversion` GCC/MiniC RV64 differential is registered as the 59th program. It checks implicit initialization/assignment/return, an explicit cast, signed/unsigned 32/64-bit conversion paths, and exact binary64 object bytes.

永久第 59 个 differential `integer_to_double_conversion` 已在 RV64/QEMU 上通过。

## Current exact frontier / 当前精确前沿

Discovery Run #956 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, all focused cast/null suites, all **59** permanent GCC/MiniC RV64 differential programs including `integer_to_double_conversion`, and frozen tiny-AES.

The unchanged cJSON source crossed:

```c
    double number = 0;
```

and the subsequent local floating initialization path. It reached the `parse_number` dispatch:

```c
        switch (buffer_at_offset(input_buffer)[i])
```

which preprocesses to:

```c
        switch (((input_buffer)->content + (input_buffer)->offset)[i])
```

with exact first diagnostic:

```text
cJSON.i:268:16: error: call to function not yet declared
```

`switch` is not yet a keyword in the C rewrite, so it is currently tokenized as an identifier and the following `(` is interpreted as an undeclared direct call. This is a new independent **switch/case/default control-flow** capability, not an integer-to-double conversion failure.

当前下一能力簇因此是 **switch / case / default**。下一分支应建立真正的语法、AST/statement 表示与 RV64 control-flow lowering，并覆盖 fallthrough、`break`、default 和嵌套作用域，而不是针对 cJSON 的字符分类表做特例。

## Validation / 验证

Run #956 is the discovery run proving the 59th differential and the new line-268 switch frontier. Final diff audit also restored `tests/frontend/type_test.c` to main formatting and kept only the single intended integer-to-double cast-contract assertion change. A latest-head clean run with that minimal diff and the updated line-268 probe is required before this branch is merged.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
