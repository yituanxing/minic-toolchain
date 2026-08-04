# C1 Real-Program Gate / C1 真实程序门禁

## Status / 状态

The compiler-first track has crossed the first real-program threshold. Development no longer relies only on isolated syntax probes: complete algorithm programs are now compiled, statically linked, executed, and compared against an external GCC reference.

编译器优先主线已经跨过第一道真实程序门槛。开发不再只依赖孤立语法探针：完整算法程序现在会被编译、静态链接、执行，并与外部 GCC 参考结果比较。

## Tool boundary / 工具边界

```text
Original C source
  |-- RISC-V GCC -O0 -----------------> reference ELF
  |
  `-- RISC-V GCC -E -> MiniC .i -> .s -> RISC-V GCC assemble/link
                                      -> MiniC ELF

reference ELF + MiniC ELF -> qemu-riscv64 -> compare exit status
```

MiniC remains the only C compiler in its path. External GCC performs target preprocessing, assembly, linking, CRT/libc supply, and the independent reference compilation.

MiniC 路径中只有 MiniC 承担 C 编译。外部 GCC 负责目标预处理、汇编、链接、CRT/libc，以及独立的参考编译。

## Accepted programs / 已验收程序

| Program / 程序 | Main stress / 主要覆盖 | MiniC | GCC |
|---|---|---:|---:|
| Euclidean GCD / 欧几里得最大公约数 | remainder loop, state rotation / 取余循环、状态轮转 | 21 | 21 |
| Fibonacci / 斐波那契 | ordered local assignments / 有顺序依赖的局部赋值 | 55 | 55 |
| Prime count / 素数计数 | nested loops, multiplication, conditions / 嵌套循环、乘法、条件 | 15 | 15 |
| Collatz iteration / Collatz 迭代 | long data-dependent loop, if/else / 长数据相关循环、分支 | 111 | 111 |

All eight static RISC-V ELFs were executed with the pinned qemu-riscv64 user-mode tool.

八个静态 RISC-V ELF 均使用固定的 qemu-riscv64 用户态工具实际执行。

## Capability base / 能力基础

The programs rely on production support for:

这些程序建立在以下生产能力之上：

- decimal `int` constants and signed RV64 word arithmetic / 十进制 `int` 常量与有符号 RV64 word 算术；
- local declaration, initialization, load, assignment, and reassignment / 局部声明、初始化、读取、赋值与重新赋值；
- `+ - * / %`, unary `+ - !` / 算术及一元运算；
- `== != < <= > >=` with normalized C truth values / 比较及规范化 C 真假值；
- `if/else`, nearest-`if` binding, compound branches / 条件语句、最近 `if` 绑定和复合分支；
- `while`, nested loops, nested IF, and return from loops / 循环、嵌套循环、循环内条件和返回；
- program-owned AST, blocks, function records, and a call-safe RV64 frame / Program 自有 AST、Block、函数记录及调用安全 RV64 栈帧。

## What this milestone does not claim / 本里程碑不代表

This is not readiness for Lua, SQLite, musl, or Linux. The current programs are intentionally single-function and freestanding in language shape. Important missing capabilities include multiple parsed functions, direct calls, parameters, pointers, arrays, global objects, richer integer types, and block scopes.

这不代表已经可以编译 Lua、SQLite、musl 或 Linux。当前程序在语言结构上仍有意保持为单函数。关键缺口包括多函数解析、直接调用、参数、指针、数组、全局对象、更丰富的整数类型和块作用域。

## Next driver / 下一驱动目标

The next capability driver is direct function composition:

下一能力驱动目标是直接函数组合：

1. migrate `main` from compatibility fields to the owned function record completely;
2. parse multiple zero-argument `int f(void)` definitions;
3. emit every owned function with an independent call-safe frame;
4. add resolved direct zero-argument call expressions;
5. validate helper calls, nested calls, and recursion;
6. then introduce integer parameters and RV64 `a0`-`a7` argument planning.

`break` and `continue` are useful but are not the current bottleneck for real-program composition. Function boundaries and ABI correctness have higher leverage for the next workload class.

`break` 和 `continue` 有价值，但不是当前真实程序组合的主要瓶颈。函数边界与 ABI 正确性对下一类负载具有更高收益。
