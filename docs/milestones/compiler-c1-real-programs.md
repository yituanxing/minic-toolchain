# C1 Real-Program Gate / C1 真实程序门禁

## Status / 状态

The compiler-first track now compiles complete single- and multi-function algorithm programs, legal C11 forward calls through explicit prototypes, and functions with up to two signed `int` parameters passed through the RV64 `a0` and `a1` boundaries.

编译器优先主线现在可以编译完整的单函数和多函数算法程序、通过显式原型完成合法 C11 前向调用，并支持通过 RV64 `a0`、`a1` 传递最多两个有符号 `int` 参数。

## Differential oracle / 差分 Oracle

```text
Lane A — GCC reference / GCC 参考线
C source -> RISC-V GCC preprocess/compile/assemble/link -> GCC ELF

Lane B — MiniC under test / MiniC 被测线
C source -> RISC-V GCC -E -> MiniC (.i -> .s)
         -> RISC-V GCC assemble/link -> MiniC ELF

GCC ELF + MiniC ELF -> qemu-riscv64
                    -> compare exit status, stdout, and stderr
```

MiniC is the only C compiler in Lane B. External GCC supplies target preprocessing, assembly, linking, CRT/libc, and the independent reference result. A mismatch retains the preprocessed input, MiniC assembly, both ELF files, captured outputs, and optional paired disassembly.

MiniC 被测线中只有 MiniC 承担 C 编译。外部 GCC 提供目标预处理、汇编、链接、CRT/libc，以及独立参考结果。出现差异时保留预处理输入、MiniC 汇编、双方 ELF、输出文件和可选的双方反汇编。

## Accepted real programs / 已验收真实程序

| Program / 程序 | Main stress / 主要覆盖 | GCC/MiniC result / 结果 |
|---|---|---:|
| Euclidean GCD / 欧几里得最大公约数 | remainder loop and state rotation / 取余循环与状态轮转 | exit 21 |
| Fibonacci / 斐波那契 | ordered local assignments / 有顺序依赖的局部赋值 | exit 55 |
| Prime count / 素数计数 | nested loops and conditions / 嵌套循环与条件 | exit 15 |
| Collatz iteration / Collatz 迭代 | long data-dependent loop / 长数据相关循环 | exit 111 |
| Function composition / 函数组合 | direct and nested calls, caller-local preservation / 直接和嵌套调用、调用者局部保持 | exit 19 |
| Function prototype / 函数原型 | repeated declarations and legal forward call / 重复声明与合法前向调用 | exit 9 |
| Function parameter / 单参数函数 | `a0`, parameter slot, extra callee local / `a0`、参数槽和被调用者额外局部 | exit 43 |
| Two parameters / 双参数函数 | nested argument calls, `a0/a1`, caller-local preservation / 嵌套实参调用、`a0/a1`、调用者局部保持 | exit 48 |

All eight programs have equal exit status, stdout, and stderr across the GCC and MiniC lanes. All eight preprocessed units also compile under the host ASan/UBSan MiniC build.

八个程序在 GCC 与 MiniC 两条流水线上的退出码、stdout 和 stderr 均一致；八个预处理翻译单元也都通过宿主 ASan/UBSan 版本 MiniC 编译。

## Implemented capability base / 已实现能力基础

Current production support includes:

当前生产能力包括：

- signed `int` constants, RV64 word arithmetic, comparisons, `if/else`, and `while` / 有符号 `int` 常量、RV64 word 算术、比较、条件和循环；
- function-local declarations, assignments, and independent owned local ranges / 函数局部声明、赋值和独立自有局部范围；
- compatible prototypes, declaration-to-definition transitions, and legal forward calls / 兼容原型、声明转定义和合法前向调用；
- modular Parser and modular RV64 backend boundaries / 模块化 Parser 与模块化 RV64 后端边界；
- zero-, one-, or two-parameter `int` functions and calls / 零个、一个或两个 `int` 参数的函数与调用；
- comma-separated parameter and argument lists / 逗号分隔的形参和实参列表；
- nested argument evaluation that preserves earlier arguments on aligned temporary stack slots / 使用对齐临时栈槽保护较早实参的嵌套实参求值；
- argument placement in `a0/a1` and callee spills to the first two function-local slots / 实参装载到 `a0/a1`，被调用者保存到前两个函数局部槽；
- call-safe frames preserving `ra` and `s0` / 保存 `ra`、`s0` 的调用安全栈帧。

## Current boundary / 当前边界

MiniC rejects undeclared calls, incompatible function declarations, duplicate parameter names, and argument-count mismatches. Functions and calls currently accept at most two `int` parameters; `main` parameters are not supported yet.

MiniC 会拒绝未声明调用、不兼容函数声明、重复参数名和实参数量不匹配。当前函数与调用最多接受两个 `int` 参数，暂不支持 `main` 参数。

## Remaining limitations / 剩余限制

Not yet implemented:

尚未实现：

- three to eight integer parameters using `a2`–`a7` / 使用 `a2`–`a7` 的三至八个整数参数；
- meaningful recursive and mutually recursive algorithms / 有实际意义的递归与互递归算法；
- block scopes and declarations inside nested blocks / 块作用域和嵌套块声明；
- pointers, arrays, global objects, structs, and richer integer types / 指针、数组、全局对象、结构体和更丰富的整数类型；
- hosted-library output programs / 使用宿主库输出的程序。

This milestone does not claim readiness for Lua, SQLite, musl, or Linux. Those workloads remain later drivers after parameters, pointers, arrays, and object storage are established.

本里程碑不代表已经可以编译 Lua、SQLite、musl 或 Linux。这些负载要在参数、指针、数组和对象存储建立后再作为后续驱动目标。

## Next driver / 下一驱动目标

The next ordered sequence is:

下一阶段顺序为：

1. generalize argument preservation and register loading for four integer parameters;
2. extend the same checked model through `a7`;
3. recursive and mutually recursive algorithm programs;
4. block scopes;
5. arrays and pointers;
6. the first small external C project.

The GCC/MiniC differential lane remains mandatory at each executable milestone.

每个可执行里程碑仍必须通过 GCC/MiniC 双轨差分。
