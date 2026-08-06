# C3 First External Project Frontier / C3 首个外部项目前沿

> Historical snapshot / 历史快照
>
> This document freezes the C3 baseline at the point where the original 400-plus-commit integration branch was closed. It is not the live tiny-AES status. The maintained current frontier is [`tests/external/tiny-aes-c/README.md`](../../tests/external/tiny-aes-c/README.md), and the executable source of truth is its exact CI probe.
>
> 本文冻结最初 400 多提交集成分支结束时的 C3 基线，不再作为 tiny-AES 动态状态页。当前维护中的前沿位于 [`tests/external/tiny-aes-c/README.md`](../../tests/external/tiny-aes-c/README.md)，可执行的最终事实来源是其中的精确 CI 探针。

## Scope / 范围

This milestone froze the first large compiler-only integration branch before continuing the pinned `tiny-AES-c` workload through short, bounded pull requests.

本里程碑冻结第一条大型“仅编译器”集成分支，后续固定的 `tiny-AES-c` 负载改用范围受限的短 PR 推进。

The toolchain boundary was and remains:

工具链边界当时及现在均为：

```text
external target preprocessing
-> MiniC compilation (.i -> RV64 .s)
-> external assembly and linking
-> QEMU execution
```

No native preprocessor, assembler, linker, libc, or self-hosting takeover was included.

本里程碑不包含原生预处理器、汇编器、链接器、libc 或自举接管。

## Accepted C3 implementation / 已验收的 C3 实现

The frozen baseline contained production support for:

冻结基线包含以下生产能力：

- dedicated Lexer, Token, and source spans / 独立 Lexer、Token 和源码区间；
- modular Parser files with lexical block scopes / 带词法块作用域的模块化 Parser；
- typed integer, pointer, record, array, `void`, and qualified values / 类型化整数、指针、记录、数组、`void` 和限定类型；
- arithmetic, comparisons, conditions, loops, assignments, lvalues, dereference, array indexing, and pointer arithmetic / 算术、比较、条件、循环、赋值、左值、解引用、数组下标和指针算术；
- function prototypes, forward calls, nested calls, recursion, mutual recursion, and 0–8 integer register parameters / 函数原型、前向调用、嵌套调用、递归、互递归和 0～8 个整数寄存器参数；
- named records, recursive array typedefs, static read-only global arrays, internal functions, and typed `void` returns / 命名记录、递归数组 typedef、静态只读全局数组、内部函数和类型化 `void` 返回；
- RV64 layout, call-safe frames, typed loads/stores, global data emission, and internal linkage emission / RV64 布局、调用安全栈帧、类型化加载/存储、全局数据发射和内部链接发射。

Later short PRs deliberately extend this baseline; those later capabilities are documented in the live project status rather than retroactively changing this accepted list.

后续短 PR 会继续扩展该基线；这些新能力记录在动态项目状态中，不回写为当时已经具备的 C3 能力。

## Frozen validation / 冻结时验证

The C3 clean-checkout CI required:

C3 干净检出 CI 当时要求：

- Debug host gate / Debug 宿主门禁；
- Release `-Werror` host gate / Release `-Werror` 宿主门禁；
- ASan/UBSan host gate / ASan/UBSan 宿主门禁；
- focused RV64/QEMU microprograms / 聚焦 RV64/QEMU 微程序；
- nineteen GCC/MiniC differential executable programs / 19 个 GCC/MiniC 双轨可执行程序；
- pinned upstream tiny-AES frontier / 固定上游 tiny-AES 前沿。

The current matrix has grown beyond this frozen count. See the root README and live external-project status for current validation numbers.

当前矩阵已经超过该冻结数量；最新验证数量请查看根 README 和动态外部项目状态。

## Pinned upstream driver / 固定上游驱动

```text
Repository: kokke/tiny-AES-c
Commit:     23856752fbd139da0b8ca6e471a13d5bcc99a08d
Mode:       AES-128 ECB
License:    Unlicense
```

The upstream `aes.c`, `aes.h`, and license blobs are verified before each probe. No MiniC-specific upstream patch is allowed.

每次探针运行前都会校验上游 `aes.c`、`aes.h` 和许可证 Blob；不允许加入 MiniC 专用上游补丁。

At the C3 freeze, this project had already driven:

在 C3 冻结点，该项目已经推动实现：

- named `struct AES_ctx` declarations / 命名 `struct AES_ctx` 声明；
- typed and `const` function prototypes / 类型化及 `const` 函数原型；
- `void` return types and definitions / `void` 返回类型和函数定义；
- typedef-based multidimensional arrays / 基于 typedef 的多维数组；
- static read-only lookup tables and initializer lists / 静态只读查找表和初始化列表；
- static internal functions and linkage conflicts / static 内部函数和链接属性冲突检查。

## Historical frontier / 历史前沿

At this milestone, the compiler had reached the body of `KeyExpansion` and stopped at its first unsupported `unsigned` local declaration. That frontier has since been passed.

在本里程碑时，编译器已经进入 `KeyExpansion` 函数体，并停在第一条不支持的 `unsigned` 局部声明。该前沿后来已经越过。

Do not update this section for every later capability. The live frontier and exact diagnostic belong in the external-project directory and CI probe.

不要为后续每项能力持续修改本节；动态前沿和精确诊断应维护在外部项目目录及 CI 探针中。

## Completion criteria / 完成标准

`tiny-AES-c` is complete only when:

只有满足以下条件，`tiny-AES-c` 才算完成：

1. the pinned AES-128 ECB core compiles without MiniC-specific upstream edits / 固定 AES-128 ECB 核心无需 MiniC 专用修改即可编译；
2. MiniC-generated assembly is externally assembled and linked / MiniC 汇编由外部工具完成汇编和链接；
3. an independently defined test-vector harness executes under QEMU / 独立定义的测试向量驱动在 QEMU 中运行；
4. GCC and MiniC lanes produce equal observable results / GCC 与 MiniC 两条流水线产生相同可观察结果；
5. focused positive and negative tests permanently cover every generalized capability introduced by the workload / 真实项目推动的每项通用能力都有永久正负测试；
6. temporary type shims are removed or proven semantically equivalent for the accepted workload / 临时类型 shim 被移除，或已证明对验收负载语义等价；
7. README, capability documentation, validation instructions, and active deviations are reviewed again / 再次审查 README、能力文档、验证说明和活跃偏离。

## Branch policy established here / 本里程碑确立的分支策略

The detailed 400-plus-commit history was preserved separately, while the accepted baseline was squash-merged. Subsequent real-software capabilities use short branches with one exact upstream boundary, permanent coverage, documentation synchronization, and squash merge.

完整的 400 多提交历史另行保留，已验收基线通过 squash 合并。后续真实软件能力使用短分支：每条分支处理一个精确上游边界，补永久门禁、同步文档后再 squash 合并。
