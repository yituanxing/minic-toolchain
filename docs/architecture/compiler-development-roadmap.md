# Compiler-first Development Roadmap / 编译器优先开发路线

## 1. Permanent development rule / 长期开发规则

MiniC evolves through **real, executable vertical slices**, but only one toolchain boundary is replaced at a time.

MiniC 通过**能够真实运行的垂直切片**推进，但每个阶段只替换一项工具链边界。

The permanent workload ladder is:

长期负载阶梯为：

```text
minimal C programs
最小 C 程序
        ↓
focused language and ABI tests
聚焦的语言与 ABI 测试
        ↓
small and common real projects
常见小型真实项目
        ↓
large projects such as Lua and SQLite
Lua、SQLite 等中型项目
        ↓
Linux source shards and full kernel builds
Linux 源码分片与完整内核构建
```

Language standards and ABI specifications define correctness. Real software decides which missing capability is implemented next.

语言标准和 ABI 规范决定正确语义；真实软件决定下一项需要实现的能力。

## 2. Active track: C compiler only / 当前主线：仅 C 编译器

The active C rewrite accepts preprocessed C and emits RISC-V assembly:

当前 C 重写接收预处理后的 C，并输出 RISC-V 汇编：

```text
input.i
  → MiniC lexer
  → parser
  → semantic analysis
  → IR
  → RISC-V code generation
  → output.s
```

The surrounding pipeline remains external:

外围流程继续使用外部工具：

```text
source.c
  → external GCC preprocessor
  → MiniC compiler: .i → .s
  → external GNU assembler
  → external GCC/GNU linker
  → external CRT and musl sysroot
  → QEMU or native execution
```

This boundary is stage-wide. MiniC must compile every C function in the accepted input; unsupported functions must fail clearly rather than silently falling back to GCC.

该边界按完整阶段划分。MiniC 必须编译已接受输入中的全部 C 函数；不支持的函数必须明确失败，不得在内部静默回退到 GCC。

## 3. Explicitly deferred tracks / 明确暂缓的主线

The following are not completion requirements for the active compiler track:

以下内容不属于当前编译器主线的完成条件：

- native C preprocessor / 原生 C 预处理器；
- native assembler / 原生汇编器；
- native linker and archiver / 原生链接器与归档器；
- native object utilities / 原生对象文件工具；
- rebuilding musl or a project libc / 自行构建 musl 或项目 libc；
- replacing compiler runtime libraries / 替换编译器运行库；
- complete self-hosting / 完整自举。

They may remain available in the frozen Python oracle as historical evidence, but they must not expand the active milestone or obscure compiler failures.

这些能力可以保留在冻结的 Python Oracle 中作为历史证据，但不得扩大当前里程碑，也不得掩盖编译器自身的问题。

## 4. Compiler capability ladder / 编译器能力阶梯

### C0 — Minimal executable functions / 最小可执行函数

Start from programs such as:

从以下程序开始：

```c
int main(void) { return 0; }
int main(void) { return 42; }
```

Required path: parse, lower, emit assembly, assemble externally, link externally, and verify the process exit status.

必须完成：解析、降低、生成汇编、外部汇编、外部链接，并验证进程退出值。

### C1 — Expressions and local state / 表达式与局部状态

- integer constants and arithmetic / 整数常量与算术；
- comparisons and logical operations / 比较与逻辑运算；
- local declarations / 局部变量声明；
- assignment and lvalues / 赋值与左值；
- unary and compound operations / 一元与复合运算。

### C2 — Control flow / 控制流

- `if` and `else`;
- `while`, `do`, and `for`;
- short-circuit evaluation / 短路求值；
- `break` and `continue`;
- `switch`, `case`, and `goto` when real workloads require them.

### C3 — Functions and ABI / 函数与 ABI

- parameters and return values / 参数与返回值；
- calls, recursion, and multiple functions / 调用、递归与多函数；
- stack frames / 栈帧；
- RISC-V psABI classification / RISC-V psABI 分类；
- separate translation units using external linking / 外部链接下的多翻译单元。

### C4 — Memory model / 内存模型

- addresses and dereference / 取地址与解引用；
- pointers and arrays / 指针与数组；
- globals and string literals / 全局变量与字符串字面量；
- aggregate addressing / 聚合对象寻址。

### C5 — C type system / C 类型系统

- integer widths and signedness / 整数宽度与符号性；
- casts and conversions / 类型转换；
- `struct`, `union`, `enum`, and `typedef`;
- initializers / 初始化器；
- function pointers / 函数指针；
- floating point and variadic calls only when their prerequisites are explicit.

### C6 — Hosted smoke tests / Hosted 环境冒烟测试

Using external headers, CRT, musl, assembler, and linker:

使用外部头文件、CRT、musl、汇编器与链接器：

- `puts` and `printf`;
- `malloc` and `free`;
- common string functions / 常见字符串函数；
- file I/O / 文件 I/O；
- callbacks such as `qsort` / `qsort` 等回调。

### C7 — Real project ladder / 真实项目阶梯

Projects are introduced one at a time with small reproducible gates. A typical order is:

真实项目按可复现的小门禁逐个引入，典型顺序为：

```text
inih or another tiny parser
cJSON / tiny-AES
zlib
libpng or a focused mbedTLS subset
Lua
SQLite
BusyBox subsets when compiler coverage justifies them
```

The exact order may change when evidence shows a better next workload.

当证据表明另一负载更适合作为下一步时，顺序可以调整。

### C8 — Linux compiler gate / Linux 编译器门禁

Linux remains a compiler workload while all surrounding stages are external:

Linux 在该阶段仅作为编译器负载，外围阶段仍由外部工具负责：

```text
external preprocessing
→ MiniC compilation
→ external assembly and linking
```

Progress expands gradually:

覆盖范围逐级扩大：

```text
1 file → 6 files → 18 files → 72 files
→ selected subsystems → full defconfig
```

A full Linux build does not authorize starting the native preprocessor automatically. That transition requires a separate milestone decision.

完整 Linux 构建通过后，也不会自动开始原生预处理器接管；该切换必须作为独立里程碑决策。

## 5. Validation order / 验证顺序

Each new compiler capability follows this order:

每项新编译器能力按以下顺序推进：

1. add a minimal positive case / 增加最小正例；
2. add negative and boundary cases / 增加负例与边界用例；
3. compare Python Oracle and C implementation at the nearest stable representation / 在最近的稳定表示上比较 Python Oracle 与 C 实现；
4. assemble and link with external tools / 使用外部工具汇编和链接；
5. run and compare observable behavior / 运行并比较可观察行为；
6. add the case to permanent regression / 加入长期回归；
7. expand to a real project only after the focused gate is stable / 聚焦门禁稳定后再扩大到真实项目。

Token, AST, semantic, IR, assembly, object, and runtime comparisons are used when meaningful; not every milestone must compare every representation.

在有意义时比较 Token、AST、语义、IR、汇编、对象文件和运行结果；并非每个里程碑都必须比较全部表示。

## 6. Import policy / 导入策略

The new repository imports only material required by the active track:

新仓库当前只导入编译器主线需要的材料：

- a frozen M46 Python compiler oracle / 冻结的 M46 Python 编译器 Oracle；
- the validated native C support and lexer slice / 已验证的 C 基础设施与 Lexer 切片；
- minimal and focused C test cases / 最小与聚焦 C 测试；
- an external-toolchain execution harness / 外部工具链执行驱动；
- provenance and manifest information / 来源与清单信息。

Large Linux corpora, build products, evidence archives, and inactive native tools stay outside Git or in separately identified historical archives.

大型 Linux 语料、构建产物、证据归档和非活跃原生工具不进入 Git，或保存在明确标识的独立历史归档中。

## 7. Review invariant / 审查不变量

At every review, ask:

每次审查都必须回答：

> Does this change make the C compiler more correct or more usable, or does it accidentally expand the project into another toolchain component?
>
> 这项修改是在提升 C 编译器的正确性或可用性，还是无意中把当前项目扩张到了另一项工具链组件？

If the latter is true, the work must be deferred, isolated as a separate experiment, or recorded as a bounded deviation.

如果属于后者，应暂缓、隔离为独立实验，或登记为范围明确的临时偏离。
