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

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, ordinary direct `.` member access on record lvalues, pointer-return function completion, bounded null-pointer constant semantics, a real RV64D double-return ABI slice, builtin cast-type lookahead for `char`/`float`/`double`, and now **unsuffixed decimal `double` floating constants**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象、record 左值普通 `.` 成员访问、指针返回函数 completion、受限空指针常量语义、真实 RV64D double 返回 ABI、`char`/`float`/`double` cast lookahead，以及当前新增的 **无后缀十进制 `double` 浮点常量**。

The lexer now forms one floating token for common decimal forms such as `0.0`, `1.`, `.5`, `1e3`, and `2.5e-4`, while preserving ordinary `record.member` dots and the existing hexadecimal-integer path. Malformed exponents are rejected explicitly. `f/F` suffixes, hexadecimal floating constants, and long double remain outside this bounded capability.

Lexer 现在会把 `0.0`、`1.`、`.5`、`1e3`、`2.5e-4` 等常见十进制形式形成单个 floating token，同时保持普通 `record.member` 的点号以及既有十六进制整数路径；非法指数会被明确拒绝。`f/F` 后缀、十六进制浮点与 long double 仍不在本能力范围内。

A floating literal is represented as a distinct `double` rvalue carrying its binary64 raw bits. The parser converts the exact token text with `strtod`, requires complete consumption and range validity, and treats same-type `(double)<double>` as an identity rather than widening general numeric cast compatibility. Cast normalization preserves the literal unchanged. RV64 materializes the raw 64-bit value in `a0`, reusing the accepted internal double-value convention and the existing `fmv.d.x` return-ABI boundary.

浮点字面量在 AST 中是独立的 `double` rvalue，并携带 binary64 raw bits。Parser 使用 `strtod` 转换精确 token 文本，要求完整消费且数值范围有效；同类型 `(double)<double>` 直接按 identity 处理，不借机放宽一般数值 cast 规则。cast normalization 原样保留该值，RV64 将 64 位 raw bits 装入 `a0`，继续复用已经验收的内部 double 表示和 `fmv.d.x` 返回 ABI 边界。

The mixed GCC↔MiniC ABI gate now also proves a real literal value end to end: MiniC compiles `return (double)123.5;`, emits exact binary64 bits `0x405ee00000000000`, returns through `fa0`, and a GCC constructor observes exactly `123.5`.

混合 GCC↔MiniC ABI 门禁现在还会端到端验证真实字面量：MiniC 编译 `return (double)123.5;`，生成精确 binary64 bits `0x405ee00000000000`，通过 `fa0` 返回，并由 GCC constructor 精确观察到 `123.5`。

The unchanged cJSON source still reaches:

```c
return (double) NAN;
```

With the project-owned headers, `NAN` preprocesses to `0.0/0.0`, so preprocessed line 118 is:

```c
        return (double) 0.0/0.0;
```

Discovery Run #814 proves both `0.0` operands and the same-type `(double)` cast are crossed. The exact next MiniC diagnostic is:

```text
cJSON.i:118:32: error: binary operator requires int operands
```

The active blocker is therefore **double binary arithmetic**, first exposed here by `double / double`. The floating-literal branch intentionally stops before implementing arithmetic so the next capability remains independently reviewable.

Discovery Run #814 已证明两个 `0.0` 操作数以及同类型 `(double)` cast 都已经越过。当前精确下一条诊断是 `binary operator requires int operands`，因此活动缺口已经明确转为 **double 二元算术**，此处首先由 `double / double` 暴露。本 floating-literal 分支刻意在算术前收口，使下一条能力继续保持可独立审查。

Run #814 passed source inventory, clang-format, Debug, Release `-Werror`, ASan/UBSan, token/lexer/front-end gates, the mixed GCC↔MiniC double-return/literal ABI test, all 45 GCC/MiniC differential programs, and frozen tiny-AES. Its only failure was the intentionally stale cJSON frontier, which exposed the line-118 double-arithmetic boundary above.

Run #814 通过 source inventory、clang-format、Debug、Release `-Werror`、ASan/UBSan、token/lexer/frontend 门禁、混合 GCC↔MiniC double-return/literal ABI 测试、45 个 GCC/MiniC 差分程序以及冻结 tiny-AES；唯一失败来自故意保留的旧 cJSON 前沿，由此暴露上述 line-118 double 算术边界。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the crossed direct-member expression at line 104, pointer-return completion at line 105, the crossed null return at line 110, the crossed double-returning function definition at lines 114/115, and the `(double) 0.0/0.0` source at line 118. It now requires the exact line-118 double-binary-arithmetic diagnostic. Crossing that boundary intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、第 104 行直接成员表达式、第 105 行指针返回 completion、第 110 行空指针返回、第 114/115 行 double 返回函数定义，以及第 118 行 `(double) 0.0/0.0` 源码；当前要求精确的 line-118 double 二元算术诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
