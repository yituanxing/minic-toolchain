# Project Principles / 项目原则

This document defines the long-term architectural direction of MiniC. It is a review standard, not a reason to block necessary experiments.

本文定义 MiniC 的长期架构方向。它用于审查和纠偏，但不应成为阻止必要实验的理由。

## 1. Real software drives priority / 真实软件决定优先级

Linux, musl, BusyBox, SQLite, Lua, and other real programs expose the next missing capability. Standards, ABI specifications, and differential testing determine the correct semantics.

Linux、musl、BusyBox、SQLite、Lua 等真实程序负责暴露下一项缺失能力；语言标准、ABI 规范和差分测试负责确定正确语义。

A workload-specific failure should be reduced to a minimal reproducer and implemented as a general language, IR, target, object, or driver capability whenever possible.

真实负载触发的问题应当缩减为最小复现，并尽量沉淀为通用的语言、IR、目标、对象格式或 Driver 能力，而不是文件名特判。

## 2. Modular monolith / 模块化单体

MiniC is one coherent toolchain with a visible compilation path. It must not grow a universal state object that mixes driver, parser, semantic, target, linker, and runtime state.

MiniC 是一个具有清晰主流程的整体工具链。不得形成同时混合 Driver、Parser、语义、目标、链接和运行状态的万能状态对象。

At the same time, the implementation must avoid speculative framework layers. A module may use several source files while exposing only a small public interface.

同时不得为了抽象而制造层层框架。一个模块可以拆成多个源码文件，但只暴露少量公共接口。

## 3. Boundaries follow evidence / 以证据确定边界

A new abstraction is justified by at least one concrete reason:

- different ownership or lifetime;
- different invariants;
- real host-platform variation;
- more than one implementation or consumer;
- independently testable behavior;
- repeated changes caused by a different concern.

新增抽象至少应当有一项现实依据：

- 所有权或生命周期不同；
- 不变量不同；
- 存在真实宿主平台差异；
- 已出现多个实现或调用者；
- 行为能够独立测试；
- 修改原因与相邻模块明显不同。

Potential future change alone is not sufficient.

“以后可能会变”本身不足以建立新层。

## 4. Clear host, target, and runtime separation / 区分宿主、目标与运行时

- **Host** controls how the compiler process reads files, creates processes, measures time, and obtains virtual memory.
- **Target** controls data layout, ABI, registers, instructions, relocation kinds, and object format.
- **Runtime** contains CRT and compiler-generated helper routines required by target programs.

- **Host（宿主）**决定编译器进程如何读文件、创建进程、计时和申请虚拟内存；
- **Target（目标）**决定数据布局、ABI、寄存器、指令、重定位和对象格式；
- **Runtime（运行时）**包含目标程序需要的 CRT 和编译器辅助函数。

These concepts must not be represented by a single `ARCH` setting or mixed state container.

三者不得被一个 `ARCH` 变量或混合状态对象替代。

## 5. Readable algorithms, real engineering / 可识别的算法与真实工程

Compiler algorithms should be visible in appropriately named modules and documented with ownership, invariants, complexity, and rationale where those details are non-obvious.

编译器算法应当出现在职责明确、名称可识别的模块中。对于不直观的实现，应记录所有权、不变量、复杂度和选择理由。

Comments must explain why, ownership, invariants, standards, ABI requirements, or non-obvious behavior. Comments that merely restate code are discouraged.

注释应解释原因、所有权、不变量、标准、ABI 要求或不直观行为；不鼓励简单复述代码。

## 6. Controlled implementation language / 受约束的实现语言

The core implementation targets ISO C11 and does not depend on GNU C extensions. The compiler may support language extensions without using those extensions in its own bootstrap-critical source.

核心实现以 ISO C11 为基线，不依赖 GNU C 扩展。编译器可以支持扩展，但自举关键源码不应依赖这些扩展。

Standard C library facilities should be used directly when they already express the required operation. Wrappers are introduced only when they add ownership semantics, allocation policy, platform isolation, or testability.

标准 C 库已经能够准确表达的操作应直接使用。只有在需要增加所有权语义、分配策略、平台隔离或可测试性时才建立封装。

## 7. Verification before replacement / 先验证，再接管

Each C migration slice should pass focused tests and differential gates before replacing the Python production path. Larger workload gates follow after local equivalence is established.

每一段 C 迁移应当先通过聚焦测试和差分门禁，再替代 Python 正式路径；局部等价建立后再扩大到真实软件回归。

Pure refactoring, behavior changes, and production-path switches should be separate commits whenever practical.

纯重构、行为变化和生产路径切换应尽量分成不同提交。

## 8. Visible temporary deviations / 临时偏离必须可见

Temporary architectural debt is allowed; invisible architectural debt is not.

允许暂时存在架构债务，但不允许架构债务不可见。

A temporary deviation must record:

- the violated rule;
- the affected scope;
- the reason it is currently necessary;
- the risks;
- concrete exit criteria.

临时偏离必须记录：

- 偏离了哪条规则；
- 影响范围；
- 当前必须这样做的原因；
- 风险；
- 可验证的退出条件。

“Refactor later / 以后重构” is not an exit criterion.

“以后重构”不构成退出条件。

## 9. Review levels / 审查层级

- Commit checks enforce mechanical rules and fast tests.
- Pull-request review checks ownership, boundaries, language mode, target-specific behavior, and validation evidence.
- Milestone review reassesses module boundaries and active deviations.

- 提交级检查负责机械规则和快速测试；
- PR 审查负责所有权、模块边界、语言模式、目标相关行为和验证证据；
- 里程碑审查重新评估整体边界与仍然活跃的偏离项。

## 10. Status words / 规范强度

- **MUST / 必须**: architectural or correctness requirement.
- **SHOULD / 应当**: default rule; a documented exception may be accepted.
- **MAY / 可以**: permitted or recommended choice.

The project should prefer a small number of enforceable rules over a large collection of ceremonial rules.

项目应当优先维护少量能够真正执行的规则，而不是堆积形式化规范。
