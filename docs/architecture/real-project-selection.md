# Real-project Selection Strategy / 真实项目选型策略

## 1. Decision / 决策

MiniC does not try to complete the C language before compiling useful software. It advances through common, recognizable, well-tested projects whose behavior can be observed directly.

MiniC 不会先追求 C 语言全集，再去编译实用软件；而是通过常见、用途明确、自带测试且结果可直接观察的项目推进。

The permanent priority is:

长期优先级为：

```text
common and useful software
常见且实用的软件
        ×
strong project-owned tests or differential oracles
项目自身测试或可靠差分 Oracle
        ×
visible milestone value
成果可见、具有里程碑感
        ×
capability overlap with later projects
与后续项目共享能力热点
        ÷
debugging and integration cost
排错与集成成本
```

A workload is not selected merely because it exposes a different corner of C. Deliberately maximizing syntax diversity can make each project slower, expand cold-path work, and postpone complete executable milestones.

项目不会仅因能够暴露不同的 C 语言角落而被选中。刻意最大化语法差异会拖慢单个项目、扩大冷门能力工作，并推迟完整可执行里程碑。

## 2. Hotspot principle / 热点原则

A capability becomes high priority when several selected projects need it on their normal build and execution paths.

当多个已选项目在正常构建与执行主路径上反复需要同一能力时，该能力成为高优先级热点。

The hotspot is therefore the intersection of real projects, not a synthetic language-feature checklist.

因此，热点来自真实项目交集，而不是人为构造的语言特性清单。

Examples include ordinary functions, pointers, arrays, records, strings, common control flow, integer conversions, allocation, function pointers, and hosted-library calls. Implementing one such capability should unblock or simplify cJSON, Lua, TinyCC, SQLite, musl, and eventually Linux at the same time.

典型热点包括普通函数、指针、数组、记录、字符串、常见控制流、整数转换、内存分配、函数指针和 Hosted 库调用。实现这类能力应同时推进或简化 cJSON、Lua、TinyCC、SQLite、musl，最终也服务于 Linux。

## 3. Gap classification / 缺口分类

Every newly exposed compiler failure is classified before implementation.

每个新暴露的编译器失败都先分类，再决定是否实现。

### A. Common cross-project capability / 多项目通用热点

Implement promptly with focused positive, negative, ABI, and runtime regression coverage.

及时实现，并增加聚焦正例、负例、ABI 与运行回归。

### B. Current project's core path / 当前项目核心路径

Implement when required to build or run the selected configuration. The implementation must remain general C semantics rather than a source-specific patch.

若当前选定配置的构建或运行主路径确实需要，则实现；实现必须保持通用 C 语义，不得为单条源码硬编码。

### C. Optional configuration or platform path / 可选配置或平台路径

Disable the optional path when the upstream project officially supports doing so and the accepted project milestone remains meaningful.

若上游正式支持关闭，且关闭后仍能形成有意义的项目里程碑，则优先关闭该可选路径。

### D. Cold extension or isolated spelling / 冷门扩展或孤立写法

Record and defer unless later projects independently expose the same requirement.

记录并延期，除非后续项目也独立暴露同一需求。

## 4. Chosen workload ladder / 已选负载阶梯

```text
tiny-AES AES-128 ECB     completed and frozen / 已完成并冻结
        ↓
cJSON                    active second project / 当前第二项目
        ↓
Lua                      interpreter and upstream tests / 解释器与项目测试
        ↓
TinyCC                   compiler compiled by MiniC / 用 MiniC 编译另一编译器
        ↓
selected Python-era projects replayed as bounded checkpoints
将 Python 版已验证项目作为范围受限的快速回放门禁
        ↓
SQLite and other large hosted applications
SQLite 等大型 Hosted 应用
        ↓
musl                     libc and user-space sysroot milestone
musl libc 与用户态 sysroot 里程碑
        ↓
Linux                    scale, integration, GNU C, and long-tail capstone
Linux 规模、集成、GNU C 与长尾能力终局门禁
```

The replay set may include zlib, libpng, mbedTLS/TF-PSA-Crypto, inih, BusyBox subsets, and other workloads already proven useful by the frozen Python compiler. They are not required to become long independent campaigns when existing hotspot support lets them pass quickly.

回放集合可以包括 zlib、libpng、mbedTLS/TF-PSA-Crypto、inih、BusyBox 子集，以及冻结 Python 编译器已经证明有价值的其他负载。当既有热点能力足以快速通过时，不要求把它们重新扩张为漫长的独立战役。

## 5. Why these projects / 选择理由

### cJSON

A small, familiar application-style library with ordinary functions, linked records, strings, allocation, parsing, printing, and testable JSON behavior. Failures can be localized to concrete object operations and serialized output more easily than failures inside a cryptographic round function.

小型且常见，具有普通函数、链式记录、字符串、内存分配、解析与打印；可通过具体 JSON 对象操作和序列化输出定位问题，排错通常比密码算法轮函数更直观。

### Lua

A recognizable complete interpreter with its own executable behavior and test suite. A MiniC-built Lua provides a strong, visible milestone rather than only another library object.

完整且广为人知的解释器，自带可执行行为和测试体系。由 MiniC 编译出的 Lua 是清晰可见的软件里程碑，而不只是另一个库目标文件。

### TinyCC

A real compiler exercises common compiler implementation patterns and creates a meaningful compiler-compiles-compiler chain. Its target and optional platform surface must be bounded rather than treated as an immediate requirement for every backend.

真实编译器项目能够覆盖常见编译器实现模式，并形成“编译器编译编译器”的有意义链路；目标架构和可选平台范围必须受控，不能立即要求覆盖全部后端。

### Linux

Linux is selected not because it is the fastest way to enumerate C syntax, but because successfully compiling the kernel is a highly visible capstone and a powerful scale/integration test. Common projects should build the hotspot foundation first; Linux then exposes the remaining high-value GNU C, build-system, scale, and long-tail gaps naturally.

选择 Linux 不是为了最快枚举 C 语法，而是因为成功编译内核具有极强的里程碑意义，同时能够检验规模和集成。前面的常见项目先搭建热点基础，随后由 Linux 自然暴露剩余高价值 GNU C、构建系统、规模和长尾缺口。

## 6. Project acceptance rule / 项目验收规则

Each selected project must define before implementation:

每个项目在实现前必须明确：

1. pinned upstream version and exact source identity / 固定上游版本与精确源码身份；
2. accepted configuration and deliberately disabled options / 已验收配置与明确关闭的选项；
3. compiler-under-test boundary / 被测编译器边界；
4. project-owned tests, independent harnesses, or GCC/MiniC differential oracle / 项目自测、独立 Harness 或 GCC/MiniC 差分 Oracle；
5. observable completion result / 可观察的完成结果；
6. permanent regression scope / 永久回归范围；
7. explicit non-goals / 明确非目标。

MiniC must compile every C function in the accepted configuration. External GCC may preprocess, assemble, link, and provide CRT/libc, but must not silently compile an unsupported C function for MiniC.

MiniC 必须编译已验收配置中的每个 C 函数。外部 GCC 可以预处理、汇编、链接并提供 CRT/libc，但不得静默替 MiniC 编译不支持的 C 函数。
