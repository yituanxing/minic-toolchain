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

cJSON is a small, common application-style C library whose core exercises linked records, strings, allocation, parsing, printing, recursion, callbacks, integer and floating-point values, and hosted-library calls. Capabilities shared with Lua, TinyCC, SQLite, musl, and Linux are prioritized over isolated syntax.

cJSON 是小型、常见的应用式 C 库，核心覆盖链式记录、字符串、内存分配、解析、打印、递归、回调、整数与浮点值以及 Hosted 库调用。与 Lua、TinyCC、SQLite、musl 和 Linux 共享的能力优先于孤立语法。

## Accepted initial boundary / 初始验收边界

The unchanged accepted core is:

```text
cJSON.c
cJSON.h
```

Initially excluded are `cJSON_Utils`, examples and packaging, Windows-only paths, optional locale support unless required by the accepted core configuration, unreviewed upstream test dependencies, and MiniC-specific source patches.

初始阶段排除 `cJSON_Utils`、示例与打包、Windows 专用路径、除非核心配置需要的可选 Locale、未审查的上游测试依赖，以及 MiniC 专用源码补丁。

External GCC may preprocess, assemble, link, and provide CRT/libc. MiniC must compile every C function in the accepted input and may not silently delegate unsupported functions to GCC.

外部 GCC 可以预处理、汇编、链接并提供 CRT/libc；MiniC 必须编译验收输入中的每个 C 函数，不得把不支持的函数静默交给 GCC。

## Current exact frontier / 当前精确前沿

The clean-checkout probe derives RV64 `size_t` from the target compiler's `__SIZE_TYPE__`:

```c
typedef long unsigned int size_t;
```

MiniC now crosses the previously recorded foundations in the unchanged cJSON core:

- native signed/unsigned LONG and target-correct RV64 `size_t`;
- incomplete tagged records and pointer self-reference;
- distinct plain `char` identity with the active RV64 unsigned-byte value behavior;
- `double` as a distinct non-integer complete object type with size/alignment 8/8;
- stable function-type identities and cJSON-style pointer-to-function record fields;
- `const` qualifiers on individual pointer declarator levels, including top-level pointer-object `const` in parameters such as `const cJSON * const object`;
- canonical function parameter types that discard only top-level parameter-object qualifiers while preserving the qualified parameter local in a function body.

MiniC 现已在未修改 cJSON 核心中越过以下基础能力：原生 LONG 与目标正确的 RV64 `size_t`、不完整标签记录与指针自引用、独立 plain `char`、8/8 布局的 `double` 对象类型、稳定函数类型身份与 Hook 函数指针字段，以及逐级指针声明器 `const`。函数参数类型只去除顶层参数对象限定，同时函数体中的参数局部变量继续保留原限定。

The unchanged source therefore crosses prototypes such as:

```c
cJSON *cJSON_GetObjectItem(const cJSON * const object,
                           const char * const string);
```

The first remaining diagnostic is now in the array-construction API:

```c
cJSON *cJSON_CreateFloatArray(const float *numbers, int count);
```

with the exact MiniC diagnostic:

```text
cJSON.i:61:38: error: expected type name
```

The active blocker is the `float` type specifier. This is a distinct C floating type and must not be implemented by aliasing it to `double`. A bounded follow-up should establish a real `float` type identity and target layout first, then continue according to the next observed cJSON frontier; floating constants, arithmetic, conversions, and ABI lowering remain separately reviewable unless the next accepted workload requires them together.

当前精确缺口已经转为 `float` 类型说明符。`float` 是独立的 C 浮点类型，不能简单别名为 `double`。下一条范围受限分支应先建立真实 `float` 类型身份和目标布局，再按 cJSON 的下一条实际诊断继续；浮点常量、算术、转换和 ABI lowering 仍按实际负载独立审查。

`tests/external/cjson/probe.sh` permanently verifies the pinned vendor identities, recreates the target-accurate preprocessing environment without network access, anchors stable early declarations plus preprocessed line 61, and requires the exact `61:38 expected type name` diagnostic. Crossing that boundary intentionally fails the gate until a later branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久校验固定 Vendor 身份，在无网络条件下重建目标正确的预处理环境，锚定稳定早期声明及预处理第 61 行，并要求精确的 `61:38 expected type name` 诊断。后续分支越过该边界时，门禁会主动失败，直到记录下一条真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results:

1. pin and verify source/license identities / 固定并校验源码与许可证身份；
2. establish exact compiler frontiers / 建立精确编译前沿；
3. compile the complete core to RV64 assembly / 完整核心编译为 RV64 汇编；
4. assemble and link an independent behavior harness / 汇编并链接独立行为 Harness；
5. compare GCC and MiniC parsing of null, booleans, numbers, strings, arrays, and objects / 差分解析主要 JSON 值；
6. compare lookup, mutation, deletion, and unformatted printing / 差分查询、修改、删除与非格式化打印；
7. run a reviewed subset of cJSON's project-owned tests / 运行经审查的 cJSON 项目自带测试子集；
8. freeze the accepted configuration as a permanent offline regression gate / 将验收配置冻结为永久离线回归门禁。

## Gap policy / 缺口处理

Failures follow `docs/architecture/real-project-selection.md`: common cross-project capabilities are generalized, core cJSON requirements are implemented without source-specific patches, officially optional paths may be disabled when the milestone remains meaningful, and isolated cold extensions are recorded for later evidence.

失败按 `docs/architecture/real-project-selection.md` 分类：多项目通用热点进行通用化实现，cJSON 核心要求不使用源码特例，上游正式可选路径可在里程碑仍有意义时关闭，孤立冷门扩展则记录并等待后续证据。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and project-owned test gates against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和项目自测门禁时，cJSON 里程碑才算完成。
