# C1 Real-Program Gate — Historical Snapshot / C1 真实程序门禁——历史快照

## Status / 状态

This file is retained as a historical milestone. It records the point at which MiniC first crossed from isolated language examples into complete algorithm programs and GCC/MiniC differential execution.

本文件作为历史里程碑保留，记录 MiniC 首次从孤立语言用例进入完整算法程序和 GCC/MiniC 双轨执行的阶段。

The original snapshot covered eight programs and at most two integer parameters. Those limits are no longer current.

原始快照覆盖八个程序和最多两个整数参数；这些限制已经不再代表当前状态。

Current status and the first upstream project frontier are documented in:

当前状态和首个上游项目前沿见：

- [`compiler-c3-tiny-aes-frontier.md`](compiler-c3-tiny-aes-frontier.md)
- [`../../README.md`](../../README.md)
- [`../../README.zh-CN.md`](../../README.zh-CN.md)

## Historical contribution / 历史贡献

This milestone established the permanent differential oracle:

该里程碑建立了长期保留的差分 Oracle：

```text
Lane A — GCC reference / GCC 参考线
C source -> RISC-V GCC compile/assemble/link -> GCC ELF

Lane B — MiniC under test / MiniC 被测线
C source -> RISC-V GCC -E -> MiniC (.i -> .s)
         -> RISC-V GCC assemble/link -> MiniC ELF

GCC ELF + MiniC ELF -> qemu-riscv64
                    -> compare exit status, stdout, and stderr
```

It also established these review rules:

同时建立了以下审查规则：

- MiniC is the only C compiler in the tested lane / 被测线中只有 MiniC 承担 C 编译；
- external GCC remains limited to preprocessing, assembly, linking, CRT/libc, and reference output / 外部 GCC 仅负责预处理、汇编、链接、CRT/libc 和参考结果；
- mismatches retain preprocessed source, assembly, ELF files, captured output, and optional disassembly / 差异会保留预处理源码、汇编、ELF、输出和可选反汇编；
- each executable capability becomes a permanent regression before expanding the workload / 每项可执行能力在扩大负载前进入永久回归。

## Superseded limits / 已被后续里程碑取代的限制

The following historical limitations have since been removed:

以下历史限制已经被后续实现消除：

- two-parameter limit / 两参数上限；
- absence of meaningful recursion and mutual recursion / 缺少实际递归和互递归；
- absence of block scopes / 缺少块作用域；
- absence of pointers and fixed arrays / 缺少指针和固定数组；
- absence of records, typedef arrays, global read-only objects, and internal functions / 缺少记录、typedef 数组、全局只读对象和内部函数；
- no external upstream project / 尚未引入外部上游项目。

This historical file must not be used as the current capability matrix.

不得再将本历史文件用作当前能力矩阵。
