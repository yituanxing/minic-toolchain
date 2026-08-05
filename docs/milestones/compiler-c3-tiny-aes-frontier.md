# C3 First External Project Frontier / C3 首个外部项目前沿

## Scope / 范围

This milestone freezes the first large compiler-only integration branch before continuing the pinned `tiny-AES-c` workload on a new, shorter branch.

本里程碑在新的短分支继续固定的 `tiny-AES-c` 负载前，冻结首条大型“仅编译器”集成分支。

The toolchain boundary remains:

工具链边界保持为：

```text
external target preprocessing
-> MiniC compilation (.i -> RV64 .s)
-> external assembly and linking
-> QEMU execution
```

No native preprocessor, assembler, linker, libc, or self-hosting takeover is included.

本里程碑不包含原生预处理器、汇编器、链接器、libc 或自举接管。

## Accepted implementation / 已验收实现

The branch contains production support for:

该分支包含以下生产能力：

- dedicated Lexer, Token, and source spans / 独立 Lexer、Token 和源码区间；
- modular Parser files with lexical block scopes / 带词法块作用域的模块化 Parser；
- typed integer, pointer, record, array, `void`, and qualified values / 类型化整数、指针、记录、数组、`void` 和限定类型；
- arithmetic, comparisons, conditions, loops, assignments, lvalues, dereference, array indexing, and pointer arithmetic / 算术、比较、条件、循环、赋值、左值、解引用、数组下标和指针算术；
- function prototypes, forward calls, nested calls, recursion, mutual recursion, and 0–8 integer register parameters / 函数原型、前向调用、嵌套调用、递归、互递归和 0～8 个整数寄存器参数；
- named records, recursive array typedefs, static read-only global arrays, internal functions, and typed `void` returns / 命名记录、递归数组 typedef、静态只读全局数组、内部函数和类型化 `void` 返回；
- RV64 layout, call-safe frames, typed loads/stores, global data emission, and internal linkage emission / RV64 布局、调用安全栈帧、类型化加载/存储、全局数据发射和内部链接发射。

## Permanent validation / 永久验证

The clean-checkout CI runs one coarse-grained Ubuntu 24.04 job so the RISC-V toolchain cache is restored only once. Within that machine it runs host configurations and target suites concurrently where safe.

干净检出 CI 使用一个粗粒度 Ubuntu 24.04 Job，使 RISC-V 工具链缓存只恢复一次；在同一虚拟机中，对安全的宿主配置和目标测试并行执行。

Required lanes:

必须通过的流水线：

- Debug host gate / Debug 宿主门禁；
- Release `-Werror` host gate / Release `-Werror` 宿主门禁；
- ASan/UBSan host gate / ASan/UBSan 宿主门禁；
- focused RV64/QEMU microprograms / 聚焦 RV64/QEMU 微程序；
- nineteen GCC/MiniC differential executable programs / 19 个 GCC/MiniC 双轨可执行程序；
- pinned upstream tiny-AES frontier / 固定上游 tiny-AES 前沿。

The differential program lane compares exit status, standard output, and standard error and retains paired artifacts on mismatch.

差分程序流水线比较退出码、标准输出和标准错误；出现差异时保留双方产物。

## Pinned upstream driver / 固定上游驱动

```text
Repository: kokke/tiny-AES-c
Commit:     23856752fbd139da0b8ca6e471a13d5bcc99a08d
Mode:       AES-128 ECB
License:    Unlicense
```

The upstream `aes.c`, `aes.h`, and license blobs are verified before each probe. No MiniC-specific upstream patch is allowed.

每次探针运行前都会校验上游 `aes.c`、`aes.h` 和许可证 Blob；不允许加入 MiniC 专用上游补丁。

Capabilities already driven by this project include:

该项目已经推动实现：

- named `struct AES_ctx` declarations / 命名 `struct AES_ctx` 声明；
- typed and `const` function prototypes / 类型化及 `const` 函数原型；
- `void` return types and definitions / `void` 返回类型和函数定义；
- typedef-based multidimensional arrays / 基于 typedef 的多维数组；
- static read-only lookup tables and initializer lists / 静态只读查找表和初始化列表；
- static internal functions and linkage conflicts / static 内部函数和链接属性冲突检查。

## Current exact frontier / 当前精确前沿

The compiler now reaches the body of `KeyExpansion`. The next fixed failure is the first `unsigned` local declaration in that function, currently reported at the pinned preprocessed location as an undeclared local.

编译器现在已经进入 `KeyExpansion` 函数体。下一处固定失败是该函数中的第一条 `unsigned` 局部声明；在固定预处理结果中，它目前被报告为未声明局部名字。

This is a frontier, not project completion.

这只是当前前沿，不代表项目完成。

## Completion criteria / 完成标准

`tiny-AES-c` is complete only when:

只有满足以下条件，`tiny-AES-c` 才算完成：

1. the pinned AES-128 ECB core compiles without MiniC-specific upstream edits / 固定 AES-128 ECB 核心无需 MiniC 专用修改即可编译；
2. MiniC-generated assembly is externally assembled and linked / MiniC 汇编由外部工具完成汇编和链接；
3. an independently defined test-vector harness executes under QEMU / 独立定义的测试向量驱动在 QEMU 中运行；
4. GCC and MiniC lanes produce equal observable results / GCC 与 MiniC 两条流水线产生相同可观察结果；
5. focused positive and negative tests permanently cover every generalized capability introduced by the workload / 真实项目推动的每项通用能力都有永久正负测试；
6. README, capability documentation, validation instructions, and active deviations are reviewed again / 再次审查 README、能力文档、验证说明和活跃偏离。

## Next branch / 下一分支

After this integration milestone is green and merged, development continues on a new branch beginning with the real frontier rather than retaining a 400-plus-commit Draft PR.

该集成里程碑全绿并合并后，将从真实前沿创建新分支继续，不再让 400 多提交的 Draft PR 持续增长。

The next expected sequence is evidence-driven:

下一阶段仍由证据决定，当前预期顺序为：

1. unsigned integer declaration syntax / unsigned 整数声明语法；
2. comma-separated local declarations / 逗号分隔局部声明；
3. narrow integer width and signedness semantics / 窄整数宽度与符号性语义；
4. byte-sized local/global layout and RV64 load/store/data directives / 字节宽度局部和全局布局，以及 RV64 加载、存储和数据指令；
5. member access and remaining control-flow or operator requirements exposed by upstream / 上游继续暴露的成员访问、控制流或运算符需求；
6. final AES-128 ECB differential execution / 最终 AES-128 ECB 双轨执行。
