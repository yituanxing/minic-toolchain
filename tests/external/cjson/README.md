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

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, ordinary direct `.` member access on record lvalues, pointer-return function completion, bounded null-pointer constant semantics, and now a **real RV64D double-return ABI slice**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象、record 左值普通 `.` 成员访问、指针返回函数 completion、受限空指针常量语义，以及当前新增的 **真实 RV64D double 返回 ABI 切片**。

The accepted double-return slice intentionally keeps MiniC's current scalar-expression convention: a returned double is represented internally as raw 64-bit bits in `a0`. At ABI boundaries, a direct double-returning call moves `fa0` to `a0` with `fmv.x.d`, and an explicit double return moves `a0` to `fa0` with `fmv.d.x`. Integer, pointer, and void call/return paths remain unchanged. Double parameters, floating constants, arithmetic, comparisons, conversions, and general double object loads/stores remain outside this slice.

当前 double 返回切片刻意保留 MiniC 现有的标量表达式约定：double 值在编译器内部暂以 `a0` 中的 64 位 raw bits 表示。在 ABI 边界，直接 double 返回调用以 `fmv.x.d` 将 `fa0` 搬到 `a0`，显式 double return 以 `fmv.d.x` 将 `a0` 搬回 `fa0`。integer、pointer、void 调用/返回路径不变；double 参数、浮点字面量、算术、比较、转换以及通用 double 对象 load/store 仍不在本切片范围内。

A dedicated mixed-toolchain runtime gate proves the ABI behavior independently of unsupported MiniC floating expressions. GCC `seed()` returns `123.5` through `fa0`; MiniC `relay()` receives and returns that value; a GCC constructor calls `relay()` and exits with 73 if the result is not exactly `123.5`. Discovery Run #804 passed this test under QEMU with exit 0 and `abi=rv64d-fa0`, while all existing host, RV64, differential, and frozen tiny-AES gates also passed.

专用混合工具链运行门禁在不依赖 MiniC 尚未支持的浮点表达式前提下验证 ABI：GCC `seed()` 通过 `fa0` 返回 `123.5`；MiniC `relay()` 接收并再次返回该值；GCC constructor 调用 `relay()`，若结果不精确等于 `123.5` 则退出 73。Discovery Run #804 在 QEMU 下以 exit 0、`abi=rv64d-fa0` 通过，同时既有 host、RV64、差分和冻结 tiny-AES 门禁全部通过。

The unchanged cJSON source therefore crosses the `double cJSON_GetNumberValue(...)` definition and reaches its first double cast:

```c
return (double) NAN;
```

With the project-owned headers, `NAN` preprocesses to `0.0/0.0`, so preprocessed line 118 is:

```c
        return (double) 0.0/0.0;
```

The exact next MiniC diagnostic is:

```text
cJSON.i:118:17: error: expected expression
```

The active blocker is now **recognizing `double` as a cast type in expression lookahead**. The general type parser already knows `double`, but the parenthesized-expression/cast discriminator does not yet include the floating type keywords. This is a frontend syntax/AST capability and is deliberately separate from the proven RV64D return ABI slice. Floating literals immediately after the cast remain a likely later frontier and must be discovered independently rather than folded into this branch.

因此当前活动缺口已经变为 **在表达式 cast lookahead 中识别 `double` 类型**。通用 type parser 已认识 `double`，但括号表达式与 cast 的判别器尚未包含浮点类型关键字；这是独立的前端语法/AST 能力，不并入已经验收的 RV64D 返回 ABI。cast 后紧跟的浮点字面量很可能成为后续前沿，但必须由真实 cJSON discovery 独立确认，而不能提前塞入本分支。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the crossed direct-member expression at line 104, pointer-return completion at line 105, the crossed null return at line 110, the crossed double-returning function definition at lines 114/115, and the new `(double) 0.0/0.0` frontier at line 118. It requires the exact line-118 diagnostic. Crossing that boundary intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、第 104 行已越过的直接成员表达式、第 105 行指针返回 completion、第 110 行已越过的空指针返回、第 114/115 行已越过的 double 返回函数定义，以及第 118 行新的 `(double) 0.0/0.0` 前沿，并要求精确的 line-118 诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
