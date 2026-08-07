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

The unchanged core now crosses the previously frozen foundations including LONG semantics, self-referential records, plain `char`, float/double object types, function-pointer record fields, per-pointer `const`, anonymous struct typedefs, zero-initialized static record objects, direct record-member access, pointer-return completion, null-pointer constants, RV64D double return, decimal double literals, same-type double `+ - * /`, and function-scope static fixed arrays.

未修改 cJSON 核心现已越过 LONG 语义、自引用 record、plain `char`、float/double 对象类型、函数指针字段、逐级 `const`、匿名 struct typedef、静态 record 全零初始化、`.` 成员访问、指针返回 completion、空指针常量、RV64D double 返回、十进制 double 字面量、同类型 double 四则运算以及函数作用域 static 定长数组等前沿。

This branch adds **direct variadic function declaration identity**. The lexer recognizes `...` by longest match without disturbing `.5` floating constants or `record.member`. `MinicFunction` records whether a direct declaration is variadic, and conflicting variadic/non-variadic redeclarations are rejected. A variadic declaration must have at least one fixed parameter.

本分支新增 **direct variadic function declaration identity**：Lexer 以最长匹配识别 `...`，不影响 `.5` 和 `record.member`；`MinicFunction` 记录 direct declaration 是否 variadic，并拒绝 variadic/non-variadic 冲突声明；`...` 前必须至少有一个固定参数。

Variadic function definitions remain intentionally unsupported because callee-side `va_list`/`va_start` is outside this capability. Variadic function-pointer fields are also rejected explicitly so `MinicFunctionType` cannot silently lose variadic identity. Extra variadic call argument lowering is **not** claimed by this branch because unchanged cJSON reaches an earlier expression boundary first.

Variadic function definition 仍明确不支持，因为 callee 侧 `va_list`/`va_start` 不属于本能力；variadic function-pointer field 也明确拒绝，避免 `MinicFunctionType` 丢失 variadic 身份。本分支同样**不**宣称已经实现额外 variadic 参数的调用 lowering，因为 unchanged cJSON 会先撞到更早的表达式前沿。

The focused gate permanently checks a matching variadic prototype redeclaration plus four negative boundaries: bare `(...)`, signature conflict, variadic definition, and variadic function-pointer field.

专项门禁永久验证同签名 variadic prototype 重声明，并锁定裸 `(...)`、签名冲突、variadic definition、variadic function-pointer field 四类负边界。

## Current exact frontier / 当前精确前沿

The project-owned `stdio.h` now contains a genuine minimal hosted declaration:

```c
int sprintf(char *buffer, const char *format, ...);
```

The pinned cJSON source remains unchanged. Preprocessed line 126 is:

```c
    sprintf(version, "%i.%i.%i", 1, 7, 19);
```

Discovery Run #840 proves the `sprintf` declaration is accepted. The exact next first diagnostic is:

```text
cJSON.i:126:20: error: global array object requires a subscript
```

Column 20 is the comma immediately after `version`. The first argument is the function-scope static array declared on line 125. MiniC currently allows global-storage arrays only when immediately subscripted, so ordinary C **array-to-pointer decay** is now the next independently reviewable capability.

Discovery Run #840 已证明 `sprintf` variadic declaration 被正确接受。当前精确 first diagnostic 是 line 126 column 20 的 `global array object requires a subscript`；该位置位于第一个参数 `version` 后的逗号。`version` 是前一行的函数作用域 static array，而 MiniC 目前只允许 global-storage array 紧跟下标，因此下一条独立能力已经明确为普通 C **array-to-pointer decay**。

After array decay is crossed, the same call is expected to expose the format string literal and later actual variadic argument handling. Those are deliberately separate until the unchanged source makes them the first diagnostic.

越过 array decay 后，同一调用预计会继续暴露格式字符串 literal，并在更后面触发实际 variadic 参数处理；在 unchanged 源码真正把它们变成 first diagnostic 之前，这些能力保持独立。

## Validation / 验证

Discovery Run #840 passed:

- source inventory and clang-format;
- Debug host checks;
- Release `-Werror`;
- ASan/UBSan;
- static-local focused gate;
- new variadic-declaration focused gate;
- RV64/QEMU focused gate;
- all 46 GCC/MiniC differential programs;
- frozen tiny-AES AES-128 ECB acceptance.

Its only failure was the intentionally stale cJSON frontier, which exposed the array-to-pointer-decay diagnostic above. The probe is now advanced to that exact diagnostic; a clean-head run is required before this branch can merge.

Run #840 通过 source inventory、clang-format、Debug、Release `-Werror`、ASan/UBSan、static-local focused、新 variadic declaration focused、RV64/QEMU、46 个 GCC/MiniC 差分程序以及冻结 tiny-AES；唯一失败是故意过期的 cJSON 前沿，由此发现上述 array-to-pointer-decay 边界。probe 已同步到该精确诊断，合并前还必须通过最新 Head 的 clean run。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
