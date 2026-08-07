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

The unchanged core now crosses the previously frozen foundations including LONG semantics, self-referential records, plain `char`, float/double object types, function-pointer record fields, per-pointer `const`, anonymous struct typedefs, zero-initialized static record objects, direct record-member access, pointer-return completion, null-pointer constants, RV64D double return, decimal double literals, same-type double `+ - * /`, function-scope static fixed arrays, direct variadic function declaration identity, contextual array-to-pointer decay, and narrow string literal expressions.

未修改 cJSON 核心现已越过 LONG 语义、自引用 record、plain `char`、float/double 对象类型、函数指针字段、逐级 `const`、匿名 struct typedef、静态 record 全零初始化、`.` 成员访问、指针返回 completion、空指针常量、RV64D double 返回、十进制 double 字面量、同类型 double 四则运算、函数作用域 static 定长数组、direct variadic function declaration identity、上下文化 array-to-pointer decay 以及窄字符串字面量等前沿。

This branch adds **variadic direct-call actual arguments and RV64 caller lowering** for integer/pointer extra arguments. Fixed parameters keep the existing declaration type checks. Variadic calls may record up to eight total actual arguments in the existing call AST. Extra arguments accept integer/pointer value paths only; floating extras remain explicitly unsupported.

本分支新增 **variadic direct-call actual arguments 与 RV64 caller lowering**，当前额外参数范围限定为 integer/pointer。固定参数继续执行既有声明类型检查；variadic call 使用现有 call AST 记录最多 8 个真实实际参数。额外参数只接受 integer/pointer value path，floating extra 继续明确不支持。

The RV64 backend now lowers the actual argument count rather than the fixed declaration count. Fixed integer parameters retain their declared conversion. Variadic integer extras use the current default-promotion/RV64 integer-register subset: non-`long` integer values are normalized through a 32-bit `addiw`, while `long` and pointers retain XLEN values. All actual arguments are then restored into `a0..a7` according to their real position before the direct call.

RV64 backend 现在按真实实际参数数 lowering，而不是固定声明参数数。固定整数参数保留声明类型转换；variadic integer extra 使用当前 default-promotion/RV64 integer-register 子集：非 `long` 整数通过 32-bit `addiw` 归一化，`long` 与 pointer 保持 XLEN 值，随后所有实际参数按真实位置恢复到 `a0..a7` 再执行 direct call。

A permanent mixed ABI gate compiles the caller with MiniC and the variadic callee with GCC. GCC consumes the MiniC-provided extras through `va_list` as `int`, promoted `char`→`int`, `long`, and `int *`. Run #864 returned exit 0, proving the actual RV64 caller ABI behavior rather than merely accepting the syntax or matching assembly text.

永久 mixed ABI gate 由 MiniC 编译 caller、GCC 编译 variadic callee；GCC 通过 `va_list` 真实读取 MiniC 提供的 `int`、`char` 默认提升后的 `int`、`long` 和 `int *`。Run #864 返回 exit 0，因此验证的是实际 RV64 caller ABI，而不是仅仅 Parser 放行或 grep 汇编。

Negative gates preserve the capability boundary: floating variadic extras are rejected, more than eight total arguments are rejected, the full fixed prefix remains mandatory, and non-variadic calls still reject extra arguments.

负例门禁同时锁住边界：floating variadic extra 拒绝、总参数超过 8 个拒绝、fixed prefix 必须完整提供，普通 non-variadic call 仍拒绝多余参数。

## Current exact frontier / 当前精确前沿

The project-owned `stdio.h` contains the minimal hosted declaration:

```c
int sprintf(char *buffer, const char *format, ...);
```

The pinned cJSON source remains unchanged. MiniC now crosses the complete call:

```c
sprintf(version, "%i.%i.%i", 1, 7, 19);
```

The next stable preprocessed line is:

```c
    if ((string1 == ((void *)0)) || (string2 == ((void *)0)))
```

Discovery Run #864 produced the exact first diagnostic:

```text
cJSON.i:131:32: error: binary operator requires int operands
```

The diagnostic occurs at the end of the first `string1 == ((void *)0)` comparison. MiniC's current binary comparison typing is integer-only, so the first new missing capability is **pointer equality / null-pointer comparison**. The following `||` has not yet become the first diagnostic and remains a separate later capability.

Run #864 的 first diagnostic 位于第一个 `string1 == ((void *)0)` 比较结束处。当前 MiniC binary comparison type 仍只接受 integer，因此下一条独立能力明确为 **pointer equality / null-pointer comparison**；后面的 `||` 尚未成为 first diagnostic，继续保持后续独立能力。

## Validation / 验证

Discovery Run #864 passed:

- source inventory and clang-format 18;
- Debug host checks;
- Release `-Werror`;
- ASan/UBSan;
- static-local focused gate;
- variadic-declaration focused gate;
- mixed MiniC→GCC variadic caller ABI gate (`exit=0`);
- RV64/QEMU focused gate;
- all 48 GCC/MiniC differential programs;
- frozen tiny-AES AES-128 ECB acceptance.

Its only failure was the intentionally stale cJSON variadic-call frontier. The probe is now advanced to line 131 column 32 and also pins the preprocessed pointer/null comparison line. A final clean-head run is required before this branch can merge.

Run #864 通过 source inventory、clang-format 18、Debug、Release `-Werror`、ASan/UBSan、static-local focused、variadic declaration focused、MiniC→GCC mixed variadic ABI（exit=0）、RV64/QEMU、全部 48 个 GCC/MiniC 差分程序以及冻结 tiny-AES；唯一失败是故意过期的 cJSON variadic-call 前沿。probe 已推进到 line 131 column 32，并锁定对应 pointer/null comparison 的预处理文本；合并前还需最新 Head final clean run。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes declared RV64/QEMU behavior tests against a GCC reference. Crossing parser frontiers or emitting assembly alone is not completion.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并在 RV64/QEMU 中通过相对 GCC 参考的行为测试时，cJSON 里程碑才算完成；仅越过 Parser 前沿或生成汇编都不算完成。
