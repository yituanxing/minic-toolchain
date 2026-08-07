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

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, and now **ordinary direct `.` member access on record lvalues**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象，以及当前新增的 **record 左值普通 `.` 成员访问**。

Direct member access reuses the existing pointer-member AST and RV64 lowering contract. The parser validates a record lvalue, forms its address, and feeds that pointer into the same MEMBER representation used by `->`. Static record globals may enter expression parsing when immediately followed by `.`, while unsupported bare global-record value use remains rejected.

直接成员访问复用既有 pointer-member AST 与 RV64 lowering 契约：Parser 校验 record 左值并取得地址，再将该指针送入与 `->` 相同的 MEMBER 表示。静态 record 全局对象在后接 `.` 时可以进入表达式解析，而尚未支持的裸全局 record 值使用继续被拒绝。

The unchanged cJSON source now crosses:

```c
return (const char*) (global_error.json + global_error.position);
```

The next exact diagnostic occurs at the closing brace of that pointer-returning function:

```text
cJSON.i:105:1: error: unsupported implicit return type
```

This diagnostic comes from MiniC's function-completion path: it currently synthesizes a default trailing return for every function and supports that synthetic completion only for integer and `void` return types. `cJSON_GetErrorPtr` returns `const char *`, so the next independently reviewable capability is **pointer-return function completion / fallthrough handling**, not additional record-member syntax.

该诊断来自 MiniC 的函数收尾路径：当前每个函数都会补一个默认尾部 return，而该 synthetic completion 目前只支持整数和 `void` 返回类型。`cJSON_GetErrorPtr` 返回 `const char *`，因此下一条独立能力是 **指针返回函数的 completion / fallthrough 处理**，而不是继续扩展 record 成员语法。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the float prototype at preprocessed line 61, the anonymous `typedef struct {` at line 97, the static `global_error` declaration at line 101, the crossed direct-member expression at line 104, and the closing brace at line 105. It requires the exact line-105 function-completion diagnostic. Crossing that boundary intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、预处理第 61 行的 `float` 原型、第 97 行匿名 `typedef struct {`、第 101 行静态 `global_error` 声明、已越过的第 104 行直接成员表达式，以及第 105 行函数闭合位置，并要求精确的 line-105 函数收尾诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
