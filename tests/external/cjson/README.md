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

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, ordinary direct `.` member access on record lvalues, pointer-return function completion, and now **bounded null-pointer constant semantics**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象、record 左值普通 `.` 成员访问、指针返回函数 completion，以及当前新增的 **受限空指针常量语义**。

The accepted null-pointer path is intentionally narrow. A pointer cast from an integer expression is accepted only when the operand is the integer literal zero; normalization preserves it through the existing value-preserving BITCAST path, while Parsed and Normalized AST verification both reject nonzero integer-to-pointer shapes. General integer-to-pointer casts such as `(int *)1` remain rejected. One-level `void *` and non-function object pointers are assignment-compatible only when pointee qualifiers are not discarded.

当前空指针路径刻意保持窄边界：只有整数表达式本身是字面量 `0` 时才允许 cast 到 pointer；normalize 通过既有值保持 BITCAST 路径承载，Parsed/Normalized AST verifier 都继续拒绝非零整数到指针的形态。`(int *)1` 等一般整数到指针转换仍然被拒绝。一级 `void *` 与非函数对象指针仅在不丢失 pointee qualifier 时允许赋值兼容。

The permanent `null_pointer_constant` differential program executes `(void *)0` through a pointer-returning helper and matches GCC under QEMU with exit 31. The unchanged negative `(int *)1` compiler gate continues to fail as required.

永久 `null_pointer_constant` 差分程序让 `(void *)0` 经由指针返回 helper 真正在 QEMU 下执行，并与 GCC 一致退出 31；既有 `(int *)1` 负向门禁仍按要求失败。

The unchanged cJSON source therefore crosses `return NULL;` in `cJSON_GetStringValue` and reaches the next function definition:

```c
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item)
```

The preprocessed signature is pinned at line 114, followed by the opening `{` at line 115. MiniC reports the unsupported return type after consuming the declarator and reaching that function body. The exact next diagnostic is:

```text
cJSON.i:115:1: error: unsupported function return type
```

The active blocker is now **a real `double` function return type**. MiniC already models `double` as a distinct complete 8-byte RV64 object type, but function definitions and execution do not yet implement floating-point return values or the RV64D calling convention. That capability is deliberately separate from null-pointer semantics.

因此当前活动缺口已经变为 **真正的 `double` 函数返回类型**。MiniC 已将 `double` 建模为独立、完整、RV64 上 8 字节的对象类型，但函数定义与执行尚未实现浮点返回值和 RV64D 调用约定；这条能力刻意与空指针语义分离。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the crossed direct-member expression at line 104, pointer-return completion at line 105, the crossed `return ((void *)0);` at line 110, the `double cJSON_GetNumberValue(...)` signature at line 114, and its opening brace at line 115. It requires the exact line-115 diagnostic. Crossing that boundary intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、第 104 行已越过的直接成员表达式、第 105 行指针返回 completion、第 110 行已越过的 `return ((void *)0);`、第 114 行 `double cJSON_GetNumberValue(...)` 签名，以及第 115 行函数体开括号，并要求精确的 line-115 诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
