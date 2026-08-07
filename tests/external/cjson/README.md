# cJSON external-project status / cJSON 外部项目状态

## Status / 状态

cJSON 1.7.19 is the active second real-project workload after the frozen tiny-AES AES-128 ECB milestone.

cJSON 1.7.19 是冻结 tiny-AES AES-128 ECB 里程碑之后的第二个活动真实项目。

This baseline stage pins the exact upstream source, records the project-selection strategy, and establishes a reproducible compiler frontier. It does not claim that MiniC already builds or runs cJSON.

当前基线阶段固定精确上游源码、记录项目选型策略，并建立可重复编译前沿；本阶段不宣称 MiniC 已经能够完整构建或运行 cJSON。

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

cJSON is a small, common, recognizable application-style C library. It processes ordinary JSON objects rather than concentrating correctness inside a cryptographic transformation. Its core uses linked records, strings, allocation, parsing, printing, recursion, callbacks/hooks, integer and floating-point values, and hosted-library calls.

cJSON 是小型、常见且用途明确的应用式 C 库。它处理普通 JSON 对象，正确性不集中在密码算法变换内部；核心代码包含链式记录、字符串、内存分配、解析、打印、递归、回调/Hook、整数与浮点值以及 Hosted 库调用。

The project is selected for hotspot value and observable behavior, not to maximize rare syntax exposure. Capabilities shared with Lua, TinyCC, SQLite, musl, and Linux have priority over optional cJSON configurations or isolated spellings.

选择它是为了共享热点和可观察行为，而不是最大化冷门语法暴露。与 Lua、TinyCC、SQLite、musl 和 Linux 共享的能力，优先于 cJSON 可选配置或孤立写法。

## Accepted initial boundary / 初始验收边界

The first accepted target is the unchanged core pair:

首个验收目标是未修改的核心文件：

```text
cJSON.c
cJSON.h
```

The following are initially outside scope:

初始阶段明确不包含：

- `cJSON_Utils`;
- examples and packaging files / 示例与打包文件；
- Windows-specific branches / Windows 专用路径；
- locale support unless the core accepted configuration later requires it / 除非核心配置需要，否则不启用 Locale；
- every upstream test dependency at once / 一次性引入全部上游测试依赖；
- MiniC-specific source patches / MiniC 专用上游补丁。

External GCC may preprocess, assemble, link, and provide CRT/libc. MiniC must compile every C function in the accepted input and may not silently delegate unsupported functions to GCC.

外部 GCC 可以预处理、汇编、链接并提供 CRT/libc。MiniC 必须编译已验收输入中的每个 C 函数，不得把不支持的函数静默交给 GCC。

## Current exact frontier / 当前精确前沿

The clean-checkout probe preprocesses the unchanged core with a minimal Hosted header surface. `size_t` remains derived from the target compiler's `__SIZE_TYPE__`, so the RV64 form is target-correct:

干净检出探针使用最小 Hosted 头环境预处理未修改核心。`size_t` 继续由目标编译器的 `__SIZE_TYPE__` 派生，因此 RV64 形式保持目标正确：

```c
typedef long unsigned int size_t;
```

MiniC accepts this declaration with native signed and unsigned LONG rank identities, C integer conversions, eight-byte RV64 layout, and full-width code generation. It introduces a tagged record before parsing its fields, keeps one stable record identity while the definition is incomplete, permits pointer self-reference, and rejects incomplete records used by value. It models plain `char` as a C type distinct from `unsigned char` while using the unsigned byte value behavior required by the active RV64 target. It also models `double` as a distinct non-integer complete object type with eight-byte size and eight-byte alignment on RV64.

MiniC 已通过原生有符号/无符号 LONG Rank、C 整数转换、RV64 八字节布局与全宽代码生成接受该声明；同时会在解析字段前引入结构体标签，在定义未完成期间保持同一稳定记录身份，允许指针自引用，并拒绝按值使用不完整记录。plain `char` 保持与 `unsigned char` 不同的 C 类型身份，同时采用当前 RV64 目标要求的 unsigned byte 值语义；`double` 也已经是独立的非整数完整对象类型，在 RV64 上大小与对齐均为八字节。

The current branch additionally gives function signatures stable type identities and parses pointer-to-function record fields. Identical signatures are interned to one identity, distinct signatures stay distinct, and function pointers do not collapse to `void *`. This is still a declarator/type-model stage: indirect calls and callback ABI lowering remain separate capabilities.

当前分支进一步为函数签名建立稳定类型身份，并解析记录字段中的 pointer-to-function 声明器。相同签名会去重为同一身份，不同签名保持不同，函数指针也不会退化成 `void *`。本阶段仍属于声明器/类型模型地基；间接调用和 callback ABI lowering 继续作为独立能力处理。

The unchanged cJSON source now crosses both hook fields:

未修改的 cJSON 源码现已越过两个 Hook 字段：

```c
typedef struct cJSON_Hooks
{
      void *(*malloc_fn)(size_t sz);
      void (*free_fn)(void *ptr);
} cJSON_Hooks;
```

It then reaches the first prototype with a top-level `const` qualifier on pointer parameters:

随后到达首个在指针参数本身施加顶层 `const` 限定的原型：

```c
cJSON *cJSON_GetObjectItem(const cJSON * const object,
                           const char * const string);
```

The next exact MiniC diagnostic is:

新的精确首条诊断为：

```text
cJSON.i:32:43: error: expected parameter name
```

The active blocker is therefore not indirect callback invocation yet. It is declarator qualification for parameter objects such as `const cJSON * const object`: MiniC understands the pointee qualifier (`const cJSON *`) but does not yet model or consume the second, top-level pointer `const`. The next bounded branch should add general pointer-object qualifier semantics rather than special-case this cJSON prototype.

因此当前缺口还不是 callback 间接调用，而是 `const cJSON * const object` 这类参数对象的声明器限定：MiniC 已能处理所指对象的 `const`（`const cJSON *`），但尚不能建模或消费第二个、作用于指针对象本身的顶层 `const`。下一条范围受限分支应建立通用 pointer-object qualifier 语义，而不是为该 cJSON 原型增加特例。

`tests/external/cjson/probe.sh` permanently verifies the vendored identities, recreates the target-accurate preprocessing environment without network access, verifies the stable early source anchors through the hook declaration, and requires this exact line-32 diagnostic. Crossing it intentionally fails the gate until the next bounded branch records the following real source boundary.

`tests/external/cjson/probe.sh` 永久校验 Vendor 身份，在无网络条件下重建目标正确的预处理环境，校验到 Hook 声明为止的稳定早期源码锚点，并要求第 32 行这条精确诊断。当 MiniC 越过此处时，门禁会主动失败，直到下一条范围受限分支记录后续真实源码边界。

## Validation ladder / 验证阶梯

The project will advance through visible, independently reviewable results:

项目通过可见且可独立审查的结果逐级推进：

1. pin and verify source/license identities / 固定并校验源码与许可证身份；
2. establish the exact first compiler frontier / 建立精确首个编译前沿；
3. compile the complete core to RV64 assembly / 完整核心编译为 RV64 汇编；
4. assemble and link an independent behavior harness / 汇编并链接独立行为 Harness；
5. compare GCC and MiniC parsing of null, booleans, numbers, strings, arrays, and objects / 差分解析 null、布尔、数字、字符串、数组和对象；
6. compare lookup, mutation, deletion, and unformatted printing / 差分查询、修改、删除与非格式化打印；
7. run a reviewed subset of cJSON's project-owned tests / 运行经审查的 cJSON 项目自带测试子集；
8. freeze the accepted configuration as a permanent offline regression gate / 将已验收配置冻结为永久离线回归门禁。

## Gap policy / 缺口处理

Failures are classified using `docs/architecture/real-project-selection.md`:

失败按 `docs/architecture/real-project-selection.md` 分类：

- common cross-project capabilities are implemented and generalized / 多项目通用热点直接实现并通用化；
- core cJSON requirements are implemented without source-specific patches / cJSON 核心要求以通用语义实现；
- officially optional paths may be disabled / 上游正式可选路径可以关闭；
- isolated cold extensions are recorded and deferred until another project confirms their value / 孤立冷门扩展先记录，等待其他项目确认价值。

## Completion result / 完成标志

The cJSON milestone is complete only when an unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and project-owned test gates against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和项目自测门禁时，cJSON 里程碑才算完成。
