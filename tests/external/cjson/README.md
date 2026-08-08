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

The unchanged core now crosses the previously frozen language features plus scalar conditions, generalized bounded `for` clauses, character constants, function-valued static relocations, bounded `sizeof`, indirect function calls, runtime function designators, bounded function-pointer null compatibility, and local `double` object storage.

本分支完成 **local double object / scalar double storage**：

- RV64 layout 继续使用已有通用 local slot 规则，`double` 已天然是 8-byte / 8-align；
- statement dispatcher 将 `double` 识别为局部声明起点；
- scalar object memory helper 将 `double` 视为 8-byte 标量；
- C 后端继续保持既有约定：double expression 的 binary64 raw bits 存在整数寄存器 `a0`，因此内存 load/store 使用 `ld` / `sd` 保留原始位模式；
- 进入 double arithmetic 时继续由既有 `fmv.d.x` / `fmv.x.d` 桥接到 FP 寄存器，double return ABI 继续在 `a0` bits 与 `fa0` 间转换；
- 没有引入第二套 local FP value representation，也没有修改已稳定的 generic local layout。

这一点与冻结 Python 编译器的成熟结构一致：局部浮点对象仍走通用 aligned local storage + typed load/store；具体寄存器表示则遵循当前 C rewrite 已冻结的 raw-bits convention。

A permanent `local_double_objects` GCC/MiniC RV64 differential is registered as the 58th program. It validates binary64 object representation through the standards-defined `unsigned char *` alias path and covers literal initialization, local-to-local copy, double arithmetic reassignment, and storing a double function return.

永久第 58 个 differential `local_double_objects` 已在 RV64/QEMU 上通过。

## Current exact frontier / 当前精确前沿

Discovery Run #948 passed source inventory, clang-format 18, Debug, Release `-Werror`, ASan/UBSan, focused suites, all **58** permanent GCC/MiniC RV64 differential programs including `local_double_objects`, and frozen tiny-AES.

The unchanged cJSON source now recognizes and lays out:

```c
    double number = 0;
```

but stops at the initializer with exact first diagnostic:

```text
cJSON.i:255:22: error: initializer type does not match local type
```

This is no longer a local-storage problem. The source initializer is integer `0`, while MiniC currently accepts only same-floating-type assignment and deliberately rejects integer-to-double conversion. The next independent capability is therefore bounded integer-to-double scalar conversion / assignment conversion.

当前下一能力簇是 **int → double conversion**。下一分支应先建立通用转换表达式/ABI lowering，并让 local initialization、assignment、return/call 等需要 assignment conversion 的语境复用，而不是只对 `double number = 0` 打特例。

## Validation / 验证

Run #948 is the discovery run proving the 58th differential and the new line-255 initializer-conversion frontier. A latest-head clean run with the updated cJSON probe is required before this branch is merged.

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
