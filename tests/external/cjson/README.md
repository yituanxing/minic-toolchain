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

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, ordinary direct `.` member access on record lvalues, pointer-return function completion, bounded null-pointer constant semantics, a real RV64D double-return ABI slice, and now **builtin cast-type lookahead for `char`, `float`, and `double`**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象、record 左值普通 `.` 成员访问、指针返回函数 completion、受限空指针常量语义、真实 RV64D double 返回 ABI 切片，以及当前新增的 **`char`、`float`、`double` builtin cast-type lookahead**。

The cast discriminator now recognizes every builtin type specifier already accepted by the general type parser. Existing conversion policy is unchanged: plain `char` casts use the established integer-cast path, while integer-to-`float` and integer-to-`double` conversions remain intentionally rejected. Focused negative fixtures lock that boundary instead of silently widening floating conversion semantics.

cast 判别器现在识别通用 type parser 已经接受的 builtin 类型说明符。既有转换策略保持不变：plain `char` cast 复用已存在的整数 cast 路径，而 integer-to-`float` 和 integer-to-`double` 仍刻意拒绝；focused 负例永久锁住该边界，避免借语法识别之名悄悄放宽浮点转换语义。

The accepted double-return slice keeps MiniC's current scalar-expression convention: a returned double is represented internally as raw 64-bit bits in `a0`. At ABI boundaries, a direct double-returning call moves `fa0` to `a0` with `fmv.x.d`, and an explicit double return moves `a0` to `fa0` with `fmv.d.x`. Double parameters, floating constants, arithmetic, comparisons, conversions, and general double object loads/stores remain outside that slice.

已验收的 double 返回切片保留 MiniC 当前的标量表达式约定：double 值在编译器内部暂以 `a0` 中的 64 位 raw bits 表示。在 ABI 边界，直接 double 返回调用以 `fmv.x.d` 将 `fa0` 搬到 `a0`，显式 double return 以 `fmv.d.x` 将 `a0` 搬回 `fa0`。double 参数、浮点字面量、算术、比较、转换以及通用 double 对象 load/store 仍在该切片之外。

The unchanged cJSON source reaches:

```c
return (double) NAN;
```

With the project-owned headers, `NAN` preprocesses to `0.0/0.0`, so preprocessed line 118 is:

```c
        return (double) 0.0/0.0;
```

Discovery Run #809 proves the cast lookahead itself is crossed. The exact next MiniC diagnostic is:

```text
cJSON.i:118:26: error: direct member access requires a record lvalue
```

The column points to the decimal point in the first `0.0`. The lexer currently emits integer constant `0`, `.` and integer constant `0`; postfix parsing therefore interprets the decimal point as direct record-member syntax. The next independently reviewable capability is **floating literal tokenization and primary-expression representation**, not additional cast syntax.

Discovery Run #809 已证明 cast lookahead 已经越过。当前精确下一条诊断落在第一个 `0.0` 的小数点：lexer 目前将其拆成整数常量 `0`、`.`、整数常量 `0`，postfix parser 因而把小数点误当成 record 直接成员语法。所以下一条可独立审查的能力是 **浮点字面量的词法 token 与 primary-expression 表示**，而不是继续扩展 cast 语法。

Run #809 also passed source inventory, clang-format, Debug, Release `-Werror`, ASan/UBSan, all focused cast gates, RV64 focused validation including the mixed GCC↔MiniC double-return ABI test, all 45 GCC/MiniC differential programs, the extended plain-char cast behavior, and frozen tiny-AES. Its only failure was the intentionally stale cJSON frontier, which exposed the line-118 floating-literal boundary above.

Run #809 同时通过 source inventory、clang-format、Debug、Release `-Werror`、ASan/UBSan、全部 focused cast 门禁、包含 GCC↔MiniC double-return ABI 的 RV64 focused 验证、45 个 GCC/MiniC 差分程序、扩展后的 plain-char cast 行为以及冻结 tiny-AES；唯一失败来自故意保留的旧 cJSON 前沿，并由此暴露上述 line-118 浮点字面量边界。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the crossed direct-member expression at line 104, pointer-return completion at line 105, the crossed null return at line 110, the crossed double-returning function definition at lines 114/115, and the `(double) 0.0/0.0` source at line 118. It now requires the exact line-118 decimal-point diagnostic. Crossing that boundary intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、第 104 行已越过的直接成员表达式、第 105 行指针返回 completion、第 110 行已越过的空指针返回、第 114/115 行已越过的 double 返回函数定义，以及第 118 行 `(double) 0.0/0.0` 源码；当前要求精确的 line-118 小数点诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
