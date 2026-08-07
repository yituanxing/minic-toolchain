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

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` object types, function-pointer record fields, per-pointer-level `const`, and now a **distinct `float` object type**.

`float` is not an alias for `double`: it has its own type identity, remains outside the integer conversion model, and has RV64 size/alignment **4/4**. This branch does not add floating constants, floating arithmetic, float↔double or integer↔floating conversions, or FP call/return ABI lowering.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double` 对象类型、函数指针字段、逐级指针 `const`，以及当前新增的**独立 `float` 对象类型**。`float` 不与 `double` 混同，在 RV64 上大小/对齐为 **4/4**；本分支不加入浮点常量、浮点算术、float/double/整数之间的值转换或 FP ABI lowering。

The unchanged source therefore crosses:

```c
cJSON *cJSON_CreateFloatArray(const float *numbers, int count);
```

The next exact source frontier is the anonymous structure used for the internal error state:

```c
typedef struct {
    const unsigned char *json;
    size_t position;
} error;
```

The exact MiniC diagnostic is:

```text
cJSON.i:97:16: error: expected record tag after 'struct'
```

The active blocker is therefore **anonymous struct definition in typedef/declarator context**, not floating-point value semantics. The next bounded branch should generalize anonymous record definitions while preserving existing tagged-record identity and incomplete-record rules.

因此当前真实缺口已经转为 **typedef/声明器上下文中的匿名结构体定义**，而不是浮点值语义。下一条范围受限分支应通用化匿名记录定义，同时保持现有标签记录身份与不完整记录规则。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the float prototype at preprocessed line 61, the anonymous `typedef struct {` at line 97, and requires the exact `97:16 expected record tag after 'struct'` diagnostic. Crossing that boundary intentionally fails the gate until a later branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、预处理第 61 行的 `float` 原型以及第 97 行的匿名 `typedef struct {`，并要求精确的 `97:16 expected record tag after 'struct'` 诊断。后续越过该边界时，门禁会主动失败，直到记录下一条真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
