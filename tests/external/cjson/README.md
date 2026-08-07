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

The clean-checkout probe derives target-correct RV64 `size_t` from `__SIZE_TYPE__`, verifies the pinned vendor blobs offline, and fixes stable preprocessed anchors before checking MiniC's first diagnostic.

MiniC now crosses the previously recorded foundations in the unchanged core, including native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, anonymous struct typedefs, zero-initialized internal static record objects, direct `.` member access, pointer-return function completion, bounded null-pointer constants, the RV64D double-return ABI slice, `char`/`float`/`double` cast lookahead, decimal `double` literals, same-type double `+ - * /`, and now **function-scope static fixed arrays with static storage duration**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`、匿名 struct typedef、内部静态 record 全零初始化对象、普通 `.` 成员访问、指针返回函数 completion、受限空指针常量、RV64D double 返回 ABI、`char`/`float`/`double` cast lookahead、十进制 `double` 字面量、同类型 double `+ - * /`，以及当前新增的 **具有静态存储期的函数作用域 static 定长数组**。

### Static-local storage model / static local 存储模型

A function-scope declaration such as:

```c
static char version[15];
```

keeps its source-level name in the parser's ordinary block-scope binding stack, while its storage is represented by a uniquely named internal `MinicGlobalObject`. The hidden object is emitted with whole-object `.zero` initialization and is not exported. Scope exit removes only the source-name binding; the storage itself remains for the lifetime of the program.

类似 `static char version[15];` 的函数作用域声明，其源码名字继续存在于 Parser 的普通 block-scope binding 栈中，而实际存储由唯一命名的 internal `MinicGlobalObject` 表示。隐藏对象以 whole-object `.zero` 初始化且不导出；离开作用域只移除源码名字绑定，实际存储在整个进程生命周期内持续存在。

This bounded capability currently accepts fixed arrays without explicit initializers. Scalar static locals, explicit static-local initializers, and function-scope static records remain intentionally unsupported rather than being partially implemented.

当前能力边界只接受无显式 initializer 的定长数组。scalar static local、显式 static-local initializer 以及函数作用域 static record 继续明确拒绝，避免形成只能声明但无法完整使用的半实现语义。

The permanent `static_local_array` GCC/MiniC RV64 differential program proves the behavior rather than only the syntax: one `static int[1]` persists across repeated calls (`1 -> 2 -> 3`), another function using the same source name receives independent storage, and a cJSON-shaped `static char version[15]` is writable and readable. The permanent differential inventory is therefore **46 programs**.

永久 `static_local_array` GCC/MiniC RV64 差分程序不仅验证语法，还验证真实行为：一个 `static int[1]` 在重复调用间保持 `1 -> 2 -> 3`，另一个函数中相同源码名字拥有独立存储，同时 cJSON 形状的 `static char version[15]` 可以正确读写。永久差分程序数量因此增加到 **46 个**。

The dedicated focused gate also checks hidden symbol generation, `.zero 4` / `.zero 15`, non-export, and the expected rejection of scalar static locals, explicit static-local initializers, and same-scope ordinary/static duplicate declarations.

专用 focused gate 还会检查隐藏符号生成、`.zero 4` / `.zero 15`、不导出，以及 scalar static、显式 initializer、普通 local/static local 同作用域重名三类边界诊断。

### New unchanged-cJSON frontier / 新的 unchanged cJSON 前沿

The unchanged core now crosses preprocessed line 124:

```c
    static char version[15];
```

and reaches line 125:

```c
    sprintf(version, "%i.%i.%i", 1, 7, 19);
```

The exact first MiniC diagnostic is now:

```text
cJSON.i:125:12: error: call to function not yet declared
```

The project-owned probe intentionally keeps hosted headers minimal. Its current `<stdio.h>` does not declare `sprintf`, while MiniC requires a direct callee to be declared before use. The next independently reviewable capability is therefore **generic hosted/variadic function declaration and direct-call modeling**, not a cJSON-specific `sprintf` exception.

当前 project-owned probe 有意保持 hosted header 极简；其中 `<stdio.h>` 尚未声明 `sprintf`，而 MiniC 要求 direct callee 在调用前已经声明。因此下一条可独立审查的能力是 **通用 hosted/variadic function declaration 与 direct-call 建模**，而不是给 cJSON 的 `sprintf` 写特例。

After a real variadic declaration becomes available, this same source line is expected to expose further independent language requirements such as ordinary array-to-pointer decay for `version`, string literals for the format text, and variadic argument handling. Those are not claimed by the static-local milestone and will be ordered by the unchanged source's next first diagnostic.

当真实 variadic declaration 可用后，同一行还会继续暴露 `version` 的普通 array-to-pointer decay、格式字符串字面量以及 variadic 参数处理等独立能力。本 static-local 里程碑不宣称已经支持这些功能；后续仍由 unchanged 源码的下一条 first diagnostic 决定实际顺序。

## Validation / 验证

GitHub Actions Run #834 passed the complete compiler gate on the branch head containing the production implementation, focused static-local gate, 46-program differential inventory, frozen tiny-AES gate, and the line-125 cJSON probe.

GitHub Actions Run #834 已在包含 production 实现、static-local focused gate、46 个差分程序、冻结 tiny-AES 以及 line-125 cJSON probe 的分支 Head 上通过完整 compiler gate。

The validated ladder includes:

- source inventory and clang-format policy;
- Debug host checks;
- Release `-Werror` host checks;
- ASan/UBSan host checks;
- dedicated static-local focused checks;
- RV64/QEMU focused checks;
- all 46 GCC/MiniC differential programs;
- frozen tiny-AES AES-128 ECB acceptance;
- unchanged pinned cJSON frontier at line 125.

Because the compiler currently stops while parsing the `sprintf` call, crossing earlier source such as `return item->valuedouble;` still must not be interpreted as complete backend validation of every preceding floating member-load path. Full-project acceptance remains reserved for the final compile/link/runtime differential milestone.

由于编译器当前仍会在解析 `sprintf` 调用时停止，越过更早的 `return item->valuedouble;` 仍不能被解释成此前所有浮点成员读取路径都已经完成后端验收。完整项目验收仍保留到最终 compile/link/runtime differential 里程碑。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查的结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
