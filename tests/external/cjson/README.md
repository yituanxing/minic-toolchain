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

The unchanged core now crosses the previously frozen foundations including LONG semantics, self-referential records, plain `char`, float/double object types, function-pointer record fields, per-pointer `const`, anonymous struct typedefs, zero-initialized static record objects, direct record-member access, pointer-return completion, null-pointer constants, RV64D double return, decimal double literals, same-type double `+ - * /`, function-scope static fixed arrays, direct variadic function declaration identity, and contextual array-to-pointer decay.

未修改 cJSON 核心现已越过 LONG 语义、自引用 record、plain `char`、float/double 对象类型、函数指针字段、逐级 `const`、匿名 struct typedef、静态 record 全零初始化、`.` 成员访问、指针返回 completion、空指针常量、RV64D double 返回、十进制 double 字面量、同类型 double 四则运算、函数作用域 static 定长数组、direct variadic function declaration identity 以及上下文化 array-to-pointer decay 等前沿。

This branch adds ordinary narrow **string literal expressions**. The lexer recognizes a quoted literal as one token, including escaped quotes/backslashes, and diagnoses raw newlines or unterminated literals. The parser decodes a bounded set of simple C escapes and materializes every literal as an internal read-only `char[N]` global object with an explicit terminal NUL.

本分支新增普通窄 **string literal expression**。Lexer 会把带引号字符串识别为一个完整 token，包括 escaped quote/backslash，并对 raw newline 和未闭合字符串给出诊断。Parser 解码一组受控的简单 C escape，并把每个 literal 物化为内部只读 `char[N]` global object，同时显式追加结尾 NUL。

The hidden object uses a `.Lminic_string_<n>` internal symbol, so it cannot collide with a legal C identifier. Existing global-array layout and RV64 emission place the bytes in `.rodata` using `.byte`; existing postfix subscript and array-to-pointer decay provide expression behavior. No string-specific RV64 expression kind is required.

隐藏对象使用 `.Lminic_string_<n>` 内部符号，不会与合法 C identifier 冲突。现有 global-array layout 与 RV64 emitter 会把字节以 `.byte` 放入 `.rodata`；现有 postfix subscript 与 array-to-pointer decay 负责表达式行为，因此无需新增 string-specific RV64 expression kind。

The accepted simple escape set in this bounded slice is `\\`, `\"`, `\'`, `\?`, `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v`, and `\0`. Octal/hex escapes and adjacent literal concatenation remain separate capabilities.

当前受控范围接受 `\\`、`\"`、`\'`、`\?`、`\a`、`\b`、`\f`、`\n`、`\r`、`\t`、`\v`、`\0`。八进制/十六进制 escape 与相邻字符串拼接仍保持独立能力。

The permanent `string_literals` GCC/MiniC RV64 differential program validates ordinary bytes, `\n`, `\"`, `\\`, automatic terminal NUL, an empty literal, multiple hidden objects, array decay, and indexed byte loads. The permanent differential inventory is now 48 programs. Lexer unit gates additionally lock escaped-quote token boundaries, unterminated-string diagnostics, and raw-newline diagnostics.

永久 `string_literals` GCC/MiniC RV64 差分程序验证普通字节、`\n`、`\"`、`\\`、自动结尾 NUL、空字符串、多个 hidden object、array decay 与 indexed byte load；永久差分程序总数现为 48。Lexer unit gate 还锁定 escaped quote 的 token 边界、未闭合字符串和 raw newline 诊断。

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

Discovery Run #857 proves both the `version` array decay and the format string literal now parse successfully. The exact next first diagnostic is:

```text
cJSON.i:126:32: error: call argument count does not match declaration
```

Column 32 is the comma following the second fixed `sprintf` argument. The direct variadic declaration is already known, but the call AST/parser still records and accepts only the fixed parameter count. Therefore **variadic direct-call actual arguments and RV64 caller lowering** are now the next independently reviewable capability.

Discovery Run #857 已证明 `version` 的 array decay 与格式字符串 literal 都已成功解析。当前精确 first diagnostic 是 line 126 column 32 的 `call argument count does not match declaration`，位置是第二个固定 `sprintf` 参数之后的逗号。也就是说 direct variadic declaration 已存在，但 call AST/Parser 仍只接收 fixed parameter count；下一条独立能力正式变成 **variadic direct-call actual arguments 与 RV64 caller lowering**。

This branch does not implement that call ABI slice. It also does not claim character literals, `sizeof(string_literal)`, adjacent literal concatenation, or octal/hex string escapes.

本分支不实现上述 variadic call ABI，也不宣称 character literal、`sizeof(string_literal)`、相邻字符串拼接或八/十六进制 string escape。

## Validation / 验证

Discovery Run #857 passed:

- source inventory and clang-format 18;
- Debug host checks;
- Release `-Werror`;
- ASan/UBSan;
- static-local focused gate;
- variadic-declaration focused gate;
- RV64/QEMU focused gate;
- all 48 GCC/MiniC differential programs, including `string_literals` with exit 0;
- frozen tiny-AES AES-128 ECB acceptance.

Its only failure was the intentionally stale cJSON string-literal frontier, which exposed the variadic-call diagnostic above. The probe is now advanced to that exact diagnostic. A final clean-head run must also include the added lexer boundary regressions before this branch can merge.

Run #857 通过 source inventory、clang-format 18、Debug、Release `-Werror`、ASan/UBSan、static-local focused、variadic declaration focused、RV64/QEMU、全部 48 个 GCC/MiniC 差分程序（其中 `string_literals exit=0`）以及冻结 tiny-AES；唯一失败是故意过期的 cJSON string-literal 前沿，由此发现上述 variadic-call 边界。probe 已同步，合并前还必须让包含新增 Lexer 边界回归的最新 Head 通过 final clean run。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
