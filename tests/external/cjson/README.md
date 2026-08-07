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

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, ordinary direct `.` member access on record lvalues, and now **pointer-return function completion**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象、record 左值普通 `.` 成员访问，以及当前新增的 **指针返回函数 completion**。

Pointer-returning functions no longer require a synthetic default pointer-valued return expression at the closing brace. Explicit pointer `return` statements retain the existing type-checking path and RV64 `a0` lowering. The permanent `pointer_return` differential program calls a pointer-returning helper, receives the pointer, dereferences it, and matches GCC with exit 29. Integer and `void` completion behavior remains unchanged.

指针返回函数在闭合花括号处不再被强制构造 synthetic 默认指针返回表达式；显式 pointer `return` 继续沿用既有类型检查与 RV64 `a0` lowering。永久 `pointer_return` 差分程序真实调用指针返回 helper、接收返回指针并解引用，和 GCC 一致退出 29；integer/`void` completion 行为保持不变。

The unchanged cJSON source therefore crosses the closing brace of `cJSON_GetErrorPtr` and enters `cJSON_GetStringValue`:

```c
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item)
{
    if (!cJSON_IsString(item))
    {
        return NULL;
    }

    return item->valuestring;
}
```

With the project-owned `<stddef.h>`, `NULL` preprocesses to `((void *)0)`. The exact next MiniC diagnostic is:

```text
cJSON.i:110:26: error: unsupported cast between these types
```

The active blocker is **C null pointer constant semantics**, specifically the integer constant zero cast to `void *` produced by `NULL`. MiniC intentionally still rejects general integer-to-pointer casts; the next branch must distinguish a valid null pointer constant from arbitrary integer-to-pointer conversion rather than weakening the general cast rule.

当前活动缺口是 **C 空指针常量语义**，具体为 `NULL` 展开出的整数常量零到 `void *` 的转换。MiniC 仍刻意拒绝一般 integer-to-pointer cast；下一条分支应准确识别合法 null pointer constant，而不是放宽任意整数到指针的转换规则。

`tests/external/cjson/probe.sh` permanently verifies stable early declarations, the float prototype at preprocessed line 61, the anonymous `typedef struct {` at line 97, the static `global_error` declaration at line 101, the crossed direct-member expression at line 104, the crossed pointer-return closing brace at line 105, and the `return ((void *)0);` expansion at line 110. It requires the exact line-110 null-pointer diagnostic. Crossing that boundary intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定稳定早期声明、预处理第 61 行的 `float` 原型、第 97 行匿名 `typedef struct {`、第 101 行静态 `global_error` 声明、第 104 行已越过的直接成员表达式、第 105 行已越过的指针返回函数闭合位置，以及第 110 行 `return ((void *)0);` 展开，并要求精确的 line-110 空指针诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
