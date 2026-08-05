# C1 Real-Program Gate / C1 真实程序门禁

## Status / 状态

The compiler-first track now compiles complete single- and multi-function algorithm programs, including legal C11 forward calls through explicit zero-argument prototypes. Development is driven by executable behavior rather than isolated syntax acceptance.

编译器优先主线现在可以编译完整的单函数和多函数算法程序，并支持通过显式零参数原型完成合法 C11 前向调用。开发以可执行行为为驱动，而不再只验证孤立语法。

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
| Euclidean GCD / 欧几里得最大公约数 | remainder loop, state rotation / 取余循环、状态轮转 | exit 21 |
| Fibonacci / 斐波那契 | ordered local assignments / 有顺序依赖的局部赋值 | exit 55 |
| Prime count / 素数计数 | nested loops and conditions / 嵌套循环与条件 | exit 15 |
| Collatz iteration / Collatz 迭代 | long data-dependent loop / 长数据相关循环 | exit 111 |
| Function composition / 函数组合 | direct calls, nested calls, caller-local preservation / 直接调用、嵌套调用、调用者局部变量保持 | exit 19 |
| Function prototype / 函数原型 | repeated declarations and legal forward call / 重复声明与合法前向调用 | exit 9 |

All six programs have equal exit status, stdout, and stderr across the GCC and MiniC lanes.

六个程序在 GCC 与 MiniC 两条流水线上的退出码、stdout 和 stderr 均一致。

## Implemented capability base / 已实现能力基础

Current production support includes:

当前生产能力包括：

- decimal signed `int` constants and RV64 word arithmetic / 十进制有符号 `int` 常量和 RV64 word 算术；
- function-local declaration, initialization, load, assignment, and reassignment / 函数局部声明、初始化、读取和重新赋值；
- `+ - * / %`, unary `+ - !`, and signed comparisons / 算术、一元运算和有符号比较；
- `if/else` and `while`, including nested control flow / 条件、循环及嵌套控制流；
- program-owned expressions, statements, blocks, locals, and function names / Program 自有表达式、语句、Block、局部变量和函数名；
- multiple zero-argument function definitions and compatible repeated prototypes / 多个零参数函数定义和兼容原型重复声明；
- declaration-to-definition state transitions with exactly one emitted body / 声明转定义状态迁移及唯一函数体生成；
- legal forward calls through explicit prototypes / 通过显式原型完成合法前向调用；
- independent local ranges and call-safe RV64 frames / 独立局部范围和调用安全 RV64 栈帧；
- resolved zero-argument direct calls, nested calls, and self-reference syntax / 已解析零参数直接调用、嵌套调用和自引用语法；
- preservation and restoration of `ra` and `s0` / `ra` 与 `s0` 的保存和恢复。

## C language boundary / C 语言边界

MiniC rejects undeclared calls instead of accepting implicit function declarations. A forward call must be preceded by a compatible explicit prototype. Prototype-only records do not emit RV64 function bodies, and `main` must be a definition.

MiniC 会拒绝未声明调用，而不是接受隐式函数声明。前向调用必须先出现兼容的显式原型。只有原型的记录不会生成 RV64 函数体，`main` 必须是真实定义。

## Remaining limitations / 剩余限制

Not yet implemented:

尚未实现：

- integer parameters and RV64 `a0`–`a7` argument passing / 整数参数及 RV64 `a0`–`a7` 传参；
- meaningful recursive algorithms with parameters / 带参数的实际递归算法；
- block scopes and declarations inside nested blocks / 块作用域和嵌套块声明；
- pointers, arrays, global objects, structs, and richer integer types / 指针、数组、全局对象、结构体和更丰富的整数类型；
- hosted-library output programs / 使用宿主库输出的程序。

This milestone does not claim readiness for Lua, SQLite, musl, or Linux. Those workloads remain later drivers after parameters, pointers, arrays, and object storage are established.

本里程碑不代表已经可以编译 Lua、SQLite、musl 或 Linux。这些负载要在参数、指针、数组和对象存储建立后再作为后续驱动目标。

## Next driver / 下一驱动目标

The next ordered sequence is:

下一阶段顺序为：

1. one integer parameter and the RV64 `a0` boundary;
2. two to eight integer parameters with `a0`–`a7`;
3. recursive and mutually recursive algorithm programs;
4. block scopes;
5. arrays and pointers;
6. the first small external C project.

The GCC/MiniC differential lane remains mandatory at each executable milestone.

每个可执行里程碑仍必须通过 GCC/MiniC 双轨差分。
