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

The unchanged core now crosses the previously frozen foundations including LONG semantics, self-referential records, plain `char`, float/double object types, function-pointer record fields, per-pointer `const`, anonymous struct typedefs, zero-initialized static record objects, direct record-member access, pointer-return completion, null-pointer constants, RV64D double return, decimal double literals, same-type double `+ - * /`, function-scope static fixed arrays, and direct variadic function declaration identity.

未修改 cJSON 核心现已越过 LONG 语义、自引用 record、plain `char`、float/double 对象类型、函数指针字段、逐级 `const`、匿名 struct typedef、静态 record 全零初始化、`.` 成员访问、指针返回 completion、空指针常量、RV64D double 返回、十进制 double 字面量、同类型 double 四则运算、函数作用域 static 定长数组以及 direct variadic function declaration identity 等前沿。

This branch adds ordinary C **array-to-pointer decay** for array lvalues used in value contexts. Local fixed arrays, function-scope static arrays, file-scope arrays, and inner array results from multidimensional subscripting can now decay to pointers without a call-argument-specific special case.

本分支新增普通 C **array-to-pointer decay**：数组 lvalue 在 value context 中可以标准退化为指针。local 定长数组、函数作用域 static 数组、文件作用域数组以及多维数组下标后得到的内层数组都使用同一转换路径，不为 `sprintf(version, ...)` 编写调用参数特例。

Decay is represented through the existing AST contract as the semantic equivalent of `&array[0]`, so existing subscript/address-of verification, normalization, and RV64 lvalue-address lowering remain the shared implementation path. The parser applies the conversion contextually rather than eagerly: an array used as the direct operand of unary `&` remains an array lvalue, while ordinary value and binary-expression contexts apply decay.

Decay 通过现有 AST contract 表示成语义等价的 `&array[0]`，继续复用已有 subscript/address-of verifier、normalization 和 RV64 lvalue-address lowering。Parser 不再在 postfix 阶段 eager decay，而是在表达式上下文中决定是否转换：作为 unary `&` 直接操作数的数组保持 array lvalue，普通 value/binary context 才执行 decay。

The contextual design is permanently guarded by the existing aggregate-pointer regression (`&rows[0]`) and the new `array_decay` RV64 differential program. The latter exercises local `int[2]`, function-scope static `int[2]`, and file-scope `static const int[2]` arrays through real pointer parameters and read/write behavior. The permanent GCC/MiniC differential inventory is now 47 programs.

上下文化设计由既有 aggregate-pointer 回归（`&rows[0]`）和新增 `array_decay` RV64 差分程序永久锁定。新程序同时覆盖 local `int[2]`、函数作用域 static `int[2]`、文件作用域 `static const int[2]`，并真实经过 pointer parameter 和读写行为；永久 GCC/MiniC 差分程序总数现为 47。

ASan also exposed an arena-lifetime defect during this work: `minic_parser_apply_array_decay()` held a raw `MinicExpression *` across expression-array growth. The helper now copies the source span before adding synthetic expressions, so no arena pointer survives a possible `realloc`.

本分支过程中 ASan 还发现一处 arena 生命周期问题：`minic_parser_apply_array_decay()` 曾在 expression array grow 期间跨 `realloc` 持有裸 `MinicExpression *`。现在 helper 会在追加 synthetic expression 前复制 source span，不再让 arena 指针跨越可能的扩容。

## Current exact frontier / 当前精确前沿

The project-owned `stdio.h` contains the minimal hosted declaration:

```c
int sprintf(char *buffer, const char *format, ...);
```

The pinned cJSON source remains unchanged. Preprocessed lines 125-126 are:

```c
    static char version[15];
    sprintf(version, "%i.%i.%i", 1, 7, 19);
```

Discovery Run #850 proves the first argument `version` now decays successfully. The exact next first diagnostic is:

```text
cJSON.i:126:22: error: unexpected character '"'
```

Column 22 is the opening double quote of the format string. The array-to-pointer-decay frontier is therefore crossed; **string literal lexical/expression support** is now the next independently reviewable capability.

Discovery Run #850 已证明第一个参数 `version` 已成功完成 array-to-pointer decay。当前精确 first diagnostic 是 line 126 column 22 的 `unexpected character '"'`，位置正是格式字符串的开双引号。因此 array-to-pointer-decay 前沿已经越过，下一条独立能力明确为 **string literal 的词法与表达式支持**。

Actual extra variadic argument lowering is still not claimed here. The unchanged source reaches the string literal before MiniC can parse far enough to validate the three extra integer variadic arguments, so that ABI slice remains separate until it becomes the real first frontier.

本分支仍不宣称已经实现额外 variadic 参数的实际调用 lowering。unchanged cJSON 会先在格式字符串处停止，尚未解析到后面的三个额外整数参数，因此该 ABI 能力继续保持独立，等真实 first frontier 推到那里再实现。

## Validation / 验证

Discovery Run #850 passed:

- source inventory and clang-format 18;
- Debug host checks;
- Release `-Werror`;
- ASan/UBSan;
- static-local focused gate;
- variadic-declaration focused gate;
- RV64/QEMU focused gate;
- all 47 GCC/MiniC differential programs, including `array_decay`;
- frozen tiny-AES AES-128 ECB acceptance.

Its only failure was the intentionally stale cJSON frontier, which exposed the string-literal diagnostic above. The probe is now advanced to that exact diagnostic; a clean-head run is required before this branch can merge.

Run #850 通过 source inventory、clang-format 18、Debug、Release `-Werror`、ASan/UBSan、static-local focused、variadic declaration focused、RV64/QEMU、全部 47 个 GCC/MiniC 差分程序（含 `array_decay`）以及冻结 tiny-AES；唯一失败是故意过期的 cJSON 前沿，由此发现上述 string-literal 边界。probe 已同步到该精确诊断，合并前还必须通过最新 Head 的 clean run。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
