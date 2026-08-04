# Implementation Language / 实现语言规范

## Status / 状态

Initial policy for the C rewrite. This document will be reviewed at each bootstrap milestone.

C 重写阶段的初始规范。每个自举里程碑都应重新审查本文件。

## 1. Language baseline / 语言基线

MiniC core production code **MUST** compile as ISO C11.

MiniC 核心生产代码**必须**能够按 ISO C11 编译。

The default development build should use:

```text
-std=c11
```

GNU C extensions are allowed in tests only when the test explicitly verifies GNU-language behavior. Bootstrap-critical production source must not depend on them.

GNU C 扩展只允许在明确验证 GNU 语言行为的测试中使用。自举关键生产源码不得依赖这些扩展。

## 2. Bootstrap language profile / 自举语言子集

The project may support more C features than it uses internally. A feature becomes available to MiniC's own source only after the current compiler implementation and bootstrap gates validate it reliably.

项目对外可以支持比自身源码更多的 C 特性。只有当前编译器实现及自举门禁已经稳定验证某项能力后，MiniC 自身源码才可以依赖它。

Initially preferred:

- fixed-width integer types from `<stdint.h>`;
- `size_t` and standard library interfaces;
- designated initializers;
- `_Static_assert`;
- `_Alignof` where supported by the active bootstrap profile;
- explicit structs and enums;
- ordinary function pointers.

初期优先使用：

- `<stdint.h>` 固定宽度整数；
- `size_t` 和标准库接口；
- 指定初始化器；
- `_Static_assert`；
- 当前自举子集已经验证的 `_Alignof`；
- 明确的结构体和枚举；
- 普通函数指针。

Initially avoided in bootstrap-critical source unless promoted by a documented decision:

- variable-length arrays;
- C11 threads and atomics;
- `_Generic`;
- `_Complex`;
- implementation-sensitive bit-field layouts;
- locale-dependent behavior;
- compiler-specific attributes;
- statement expressions, `typeof`, and other GNU syntax.

在通过正式决策提升之前，自举关键源码暂不使用：

- 变长数组；
- C11 线程和原子操作；
- `_Generic`；
- `_Complex`；
- 依赖实现细节的位域布局；
- Locale 相关行为；
- 编译器专用 Attribute；
- Statement expression、`typeof` 等 GNU 语法。

## 3. Standard library policy / 标准库策略

Standard C library functions **SHOULD** be called directly when they already express the required operation and do not hide an ownership or platform boundary.

标准 C 库已经准确表达所需操作，并且不涉及额外所有权或平台边界时，**应当**直接调用。

Examples:

```text
memcpy, memmove, memset, memcmp
strlen, strcmp, strncmp, strchr
malloc, realloc, free
snprintf
```

Do not add project-prefixed wrappers that merely rename these functions.

不得为这些函数增加仅仅改名的项目封装。

A project abstraction is justified when it adds one or more of:

- ownership and lifetime semantics;
- an allocation strategy such as an arena;
- host-platform isolation;
- failure injection or test control;
- a data representation not provided by standard C, such as `StringView` or interned `StringId`.

只有在增加以下真实语义时才建立项目抽象：

- 所有权和生命周期；
- Arena 等分配策略；
- 宿主平台隔离；
- 故障注入或测试控制；
- 标准 C 未提供的数据表示，例如 `StringView` 或驻留后的 `StringId`。

## 4. Non-standard library functions / 非标准库函数

Core modules **MUST NOT** depend directly on POSIX- or GNU-only convenience functions unless the dependency is isolated in the host platform module or registered as a temporary deviation.

核心模块**不得**直接依赖 POSIX 或 GNU 专用便利函数，除非该依赖被隔离在宿主平台模块中，或登记为临时偏离。

Examples include:

```text
strdup, strndup, getline, asprintf, realpath, strcasecmp
```

Equivalent project helpers may be introduced only when they define allocation, ownership, length, termination, or error behavior explicitly.

只有在明确规定分配、所有权、长度、结尾和错误行为时，才可以建立等价的项目辅助函数。

## 5. Allocation / 内存分配

The default allocator may use `malloc`, `realloc`, and `free`. This does not bind the compiler core to Linux because these are standard C library interfaces.

默认分配器可以使用 `malloc`、`realloc` 和 `free`。这些是标准 C 库接口，不会把编译器核心绑定到 Linux。

High-volume objects with a shared lifetime, including tokens, AST nodes, symbols, types, and IR nodes, should use an arena or another documented region-allocation strategy when measurements and ownership justify it.

Token、AST 节点、符号、类型、IR 节点等生命周期一致的大量对象，在测量结果和所有权关系证明合理时，应使用 Arena 或其他明确记录的区域分配策略。

The core must not call `brk`, `mmap`, `VirtualAlloc`, or similar operating-system interfaces directly. Page-level virtual-memory operations belong to the host platform module.

核心不得直接调用 `brk`、`mmap`、`VirtualAlloc` 等操作系统接口。页级虚拟内存操作属于宿主平台模块。

## 6. Host platform boundary / 宿主平台边界

Linux/POSIX is the first supported host environment, but it is not the core language contract.

Linux/POSIX 是首个支持的宿主环境，但不是核心语言契约。

Only genuinely platform-dependent operations should enter `src/platform/`, such as:

- process creation;
- virtual-memory page reservation;
- high-resolution monotonic time;
- dynamic-library loading;
- path and executable discovery where standard C is insufficient.

只有真正具有平台差异的操作才进入 `src/platform/`，例如：

- 创建进程；
- 保留虚拟内存页；
- 高精度单调时钟；
- 动态库加载；
- 标准 C 不足以处理的路径和可执行文件查找。

The platform module should remain a thin compile-time-selected implementation, not a runtime service hierarchy.

平台模块应保持为编译期选择的薄实现，不建立运行时服务层级。

## 7. Diagnostics and warnings / 诊断与警告

Development builds **SHOULD** enable strict compiler warnings. Warning suppressions must be local and justified.

开发构建**应当**启用严格警告。警告抑制必须局部且有明确理由。

The initial GCC/Clang warning profile may include:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wstrict-prototypes -Wmissing-prototypes
```

The exact set may evolve when warnings are proven noisy or unsupported across bootstrap compilers. Such changes should be documented rather than silently removed.

当某些警告被证明噪声过大或自举编译器不支持时，可以调整集合，但必须记录原因，不得静默删除。

## 8. Review / 审查

Changes to this policy require either:

- an architecture decision record;
- a documented bootstrap requirement;
- evidence from a supported host compiler or real workload;
- a temporary deviation with explicit exit criteria.

修改本规范需要具备以下至少一项依据：

- 架构决策记录；
- 明确的自举要求；
- 支持的宿主编译器或真实负载证据；
- 带有具体退出条件的临时偏离记录。
