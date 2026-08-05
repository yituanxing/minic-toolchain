# C1 Real-Program Gate / C1 真实程序门禁

## Status / 状态

The compiler-first track has crossed the first real-program threshold. Development no longer relies only on isolated syntax probes: complete algorithm programs are compiled, statically linked, executed, and compared against an external GCC reference.

编译器优先主线已经跨过第一道真实程序门槛。开发不再只依赖孤立语法探针：完整算法程序会被编译、静态链接、执行，并与外部 GCC 参考结果比较。

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

MiniC remains the only C compiler in Lane B. External GCC performs target preprocessing, assembly, linking, CRT/libc supply, and the independent all-GCC reference lane. A mismatch retains `.i`, MiniC assembly, both ELF files, captured output, and optional paired disassembly.

MiniC 被测线中只有 MiniC 承担 C 编译。外部 GCC 负责目标预处理、汇编、链接、CRT/libc，并独立完成全 GCC 参考线。出现不一致时保留 `.i`、MiniC 汇编、双方 ELF、输出捕获文件，以及可选的双方反汇编。

## Accepted programs / 已验收程序

| Program / 程序 | Main stress / 主要覆盖 | Expected exit / 预期退出码 |
|---|---|---:|
| Euclidean GCD / 欧几里得最大公约数 | remainder loop, state rotation / 取余循环、状态轮转 | 21 |
| Fibonacci / 斐波那契 | ordered local assignments / 有顺序依赖的局部赋值 | 55 |
| Prime count / 素数计数 | nested loops, multiplication, conditions / 嵌套循环、乘法、条件 | 15 |
| Collatz iteration / Collatz 迭代 | long data-dependent loop, if/else / 长数据相关循环、分支 | 111 |

The historical acceptance run compared the exit status of both static RISC-V ELFs. The strengthened gate now additionally compares stdout and stderr and must be rerun after the frame migration before this milestone is merged.

历史验收运行比较了双方静态 RISC-V ELF 的退出码。强化后的门禁还会比较 stdout 与 stderr；调用安全栈帧迁移后，里程碑合并前必须重新执行。

## Implemented capability base / 已实现能力基础

The current production implementation includes:

当前生产实现包括：

- decimal `int` constants and signed RV64 word arithmetic / 十进制 `int` 常量与有符号 RV64 word 算术；
- local declaration, initialization, load, assignment, and reassignment / 局部声明、初始化、读取、赋值与重新赋值；
- `+ - * / %`, unary `+ - !` / 算术及一元运算；
- `== != < <= > >=` with normalized C truth values / 比较及规范化 C 真假值；
- `if/else`, nearest-`if` binding, and compound branches / 条件语句、最近 `if` 绑定和复合分支；
- program-owned expressions, locals, statements, and blocks / Program 自有表达式、局部变量、语句和 Block；
- a single parsed `main` function shape / 单个已解析的 `main` 函数结构；
- a call-safe single-function RV64 frame / 调用安全的单函数 RV64 栈帧。

## RV64 frame contract / RV64 栈帧契约

The local-storage base is now callee-saved `s0`. Every generated frame reserves slots for the incoming `s0` and `ra`, saves them before the body, and restores them before releasing the frame. Local accesses no longer depend on caller-saved `t1`. Save-slot and local addresses beyond the signed 12-bit load/store offset are explicitly materialized.

局部存储基址已经迁移到被调用者保存寄存器 `s0`。每个生成栈帧为传入的 `s0` 和 `ra` 预留槽位，在函数体前保存，并在释放栈帧前恢复。局部访问不再依赖调用者保存寄存器 `t1`。保存槽和局部地址超出有符号 12 位访存偏移时，会显式构造地址。

The implementation and structural tests are committed. A C11 syntax build has passed; fast, sanitizer, focused RISC-V/QEMU, and real-program GCC/MiniC differential gates remain required after this migration.

实现和结构门禁已经提交，C11 语法构建已经通过；迁移后仍必须执行快速门禁、Sanitizer、聚焦 RISC-V/QEMU 门禁，以及真实程序 GCC/MiniC 差分门禁。

## Remaining limitations / 剩余限制

The AST does not yet own function records. Multiple functions, calls, parameters, recursion, pointers, arrays, global objects, richer integer types, and block scopes remain unimplemented.

AST 当前尚未拥有函数记录。多函数、调用、参数、递归、指针、数组、全局对象、更丰富的整数类型和块作用域仍未实现。

## Next driver / 下一驱动目标

The next sequence remains deliberately ordered around evidence:

下一阶段继续按证据顺序推进：

1. rerun fast, sanitizer, micro-runtime, and GCC/MiniC real-program differential gates after the call-safe frame migration;
2. introduce program-owned function records and migrate the existing `main` into the first record;
3. parse and emit multiple zero-argument `int f(void)` definitions;
4. add resolved direct calls, nested calls, and recursion;
5. then introduce integer parameters and RV64 `a0`-`a7` argument passing.

`break` and `continue` remain useful but are not the current bottleneck for real-program composition. Function boundaries and ABI correctness have higher leverage for the next workload class.

`break` 和 `continue` 仍然有价值，但不是当前真实程序组合的主要瓶颈。函数边界与 ABI 正确性对下一类负载具有更高收益。
