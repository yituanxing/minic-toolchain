# cJSON external-project status / cJSON 外部项目状态

## Status / 状态

cJSON 1.7.19 is the active second real-project workload after the frozen tiny-AES AES-128 ECB milestone.

cJSON 1.7.19 是冻结 tiny-AES AES-128 ECB 里程碑之后的第二个活动真实项目。

The project uses the unchanged pinned core as a reproducible compiler-frontier workload. It does not yet claim that MiniC builds or runs the complete cJSON core.

该项目使用未修改的固定核心作为可重复的编译器前沿负载；当前仍不宣称 MiniC 已经完整构建或运行 cJSON 核心。

## Upstream identity / 上游身份

- Repository / 仓库：`DaveGamble/cJSON`
- Release / 版本：`1.7.19` (`v1.7.19`)
- Commit / 提交：`c859b25da02955fef659d658b8f324b5cde87be3`
- License / 许可证：MIT
- Vendored source / 入库源码：`tests/vendor/cjson/upstream/`
- Provenance / 来源记录：`tests/vendor/cjson/PROVENANCE`

Pinned Git Blob identities / 固定 Git Blob：

| File | Git blob SHA-1 |
|---|---|
| `cJSON.c` | `6e4fb0dd369cd905923da515be87ab06db6c1ee0` |
| `cJSON.h` | `cab5feb427725f8e5c82287f7fe59481b609b9b5` |
| `LICENSE` | `78deb0406d713ab9730e3c2447be1abdbd70b9a2` |

## Why this project / 选择理由

cJSON is a compact application-style C library whose unchanged core exercises linked records, strings, allocation, parsing, printing, recursion, callbacks, integer and floating-point types, and hosted-library calls. Shared cross-project capabilities are prioritized over cJSON-specific workarounds.

cJSON 是紧凑的应用式 C 库，未修改核心覆盖链式记录、字符串、内存分配、解析、打印、递归、回调、整数/浮点类型以及 Hosted 库调用。优先实现多项目共享能力，而不是 cJSON 专用绕行。

## Accepted boundary / 验收边界

The unchanged accepted core remains:

```text
cJSON.c
cJSON.h
```

External GCC may preprocess, assemble, link, and provide CRT/libc. MiniC must compile every C function in the accepted input and may not silently delegate unsupported functions to GCC.

外部 GCC 可以预处理、汇编、链接并提供 CRT/libc；MiniC 必须编译验收输入中的每个 C 函数，不得把不支持的函数静默交给 GCC。

## Current exact frontier / 当前精确前沿

The clean-checkout probe derives target-correct RV64 `size_t` from `__SIZE_TYPE__` and verifies the pinned source identities offline.

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, ordinary direct `.` member access on record lvalues, pointer-return function completion, bounded null-pointer constant semantics, a real RV64D double-return ABI slice, builtin cast-type lookahead for `char`/`float`/`double`, unsuffixed decimal `double` floating constants, and now **same-type double binary arithmetic for `+`, `-`, `*`, and `/`**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象、record 左值普通 `.` 成员访问、指针返回函数 completion、受限空指针常量语义、真实 RV64D double 返回 ABI、`char`/`float`/`double` cast lookahead、无后缀十进制 `double` 浮点常量，以及当前新增的 **同类型 double `+`、`-`、`*`、`/` 二元算术**。

The lexer forms one floating token for common decimal forms such as `0.0`, `1.`, `.5`, `1e3`, and `2.5e-4`, while preserving ordinary `record.member` dots and the existing hexadecimal-integer path. A floating literal is represented as a distinct `double` rvalue carrying binary64 raw bits. RV64 materializes those bits in `a0`, preserving the accepted internal double-value convention.

Lexer 会把 `0.0`、`1.`、`.5`、`1e3`、`2.5e-4` 等常见十进制形式形成单个 floating token，同时保持普通 `record.member` 点号以及既有十六进制整数路径。浮点字面量在 AST 中是独立的 `double` rvalue，并携带 binary64 raw bits；RV64 将这些 raw bits 装入 `a0`，延续已验收的内部 double 表示。

For double arithmetic, both operands are evaluated through that raw-bit convention. RV64 temporarily moves them into `ft0` and `ft1`, executes `fadd.d`, `fsub.d`, `fmul.d`, or `fdiv.d`, and moves the result bits back to `a0`. Integer, pointer, remainder, shift, bitwise, comparison, and mixed numeric rules are unchanged.

对于 double 算术，两个操作数继续按 raw-bit 约定求值；RV64 临时将其搬入 `ft0`/`ft1`，执行 `fadd.d`、`fsub.d`、`fmul.d` 或 `fdiv.d`，再将结果 raw bits 搬回 `a0`。整数、指针、余数、移位、位运算、比较以及 mixed numeric 规则保持不变。

The mixed GCC↔MiniC RV64D gate independently observes MiniC results for `1.5 + 2.25`, `9.0 - 2.5`, `1.5 * 4.0`, `9.0 / 4.0`, and verifies that `0.0 / 0.0` produces NaN. Discovery Run #818 passed all of these behavior checks, along with the existing exact `123.5` literal/return ABI test.

混合 GCC↔MiniC RV64D 门禁会独立观察 MiniC 对 `1.5 + 2.25`、`9.0 - 2.5`、`1.5 * 4.0`、`9.0 / 4.0` 的结果，并验证 `0.0 / 0.0` 产生 NaN。Discovery Run #818 已通过这些真实行为检查，同时继续通过既有精确 `123.5` 字面量/返回 ABI 测试。

The unchanged cJSON source therefore crosses:

```c
return (double) 0.0/0.0;
```

and parser discovery continues beyond `return item->valuedouble;` into `cJSON_Version`. The next exact preprocessed source line is:

```c
    static char version[15];
```

at line 124. The first MiniC diagnostic is:

```text
cJSON.i:124:5: error: expected compound, if, while, for, break, declaration, expression, return, or '}'
```

The next independently reviewable frontend capability is therefore **function-scope static local objects**, first exposed by a static local character array. This only establishes the first parser frontier: because translation-unit parsing stops at line 124 before code generation, reaching `return item->valuedouble;` does not yet constitute a complete backend validation of double record-member loads.

因此 unchanged cJSON 已越过 `(double) 0.0/0.0`，Parser 也继续越过 `return item->valuedouble;` 进入 `cJSON_Version`。当前精确下一行是预处理第 124 行 `static char version[15];`，第一条诊断落在函数体内 `static`。所以下一条可独立审查的前端能力是 **函数作用域 static local object**，此处首先由静态局部 char 数组暴露。需要特别说明：translation unit 在 line 124 的解析阶段即停止，因此越过 `return item->valuedouble;` 只表示 Parser 前沿已前进，并不等于 double record member load 的后端已经完整验证。

Run #818 passed source inventory, clang-format, Debug, Release `-Werror`, ASan/UBSan, all focused front-end gates, RV64 focused validation, the extended mixed GCC↔MiniC double arithmetic/NaN ABI test, all 45 GCC/MiniC differential programs, and frozen tiny-AES. Its only failure was the intentionally stale cJSON frontier, which exposed the line-124 static-local boundary above.

Run #818 通过 source inventory、clang-format、Debug、Release `-Werror`、ASan/UBSan、全部 focused frontend 门禁、RV64 focused 验证、扩展后的 GCC↔MiniC double 四则运算/NaN ABI 测试、45 个 GCC/MiniC 差分程序以及冻结 tiny-AES；唯一失败来自故意保留的旧 cJSON 前沿，由此暴露上述 line-124 static local 边界。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the crossed direct-member expression at line 104, pointer-return completion at line 105, the crossed null return at line 110, the crossed double-returning function definition at lines 114/115, the crossed double arithmetic at line 118, and the new static-local source at line 124. It now requires the exact line-124 statement-dispatch diagnostic. Crossing that boundary intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、第 104 行直接成员表达式、第 105 行指针返回 completion、第 110 行空指针返回、第 114/115 行 double 返回函数定义、第 118 行已越过的 double 算术，以及第 124 行新的 static local 源码；当前要求精确的 line-124 statement-dispatch 诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
