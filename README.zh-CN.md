# MiniC 工具链

MiniC 是一套由真实软件驱动、使用 ISO C11 重写的编译器工具链，目标同时包括真实可用、分阶段自举、长期可扩展和编译器教学。

真实负载决定下一项开发优先级；语言标准、目标 ABI、差分验证、明确的所有权规则和架构边界负责判断实现是否正确。

英文介绍：[`README.md`](README.md)

## 当前编译边界

当前主线只替换 C 编译阶段：

```text
C 源码
  -> 外部 RISC-V GCC 预处理
  -> MiniC 编译器：预处理 C（.i）生成 RV64 汇编（.s）
  -> 外部 GNU 汇编和链接
  -> QEMU RISC-V 用户态执行
```

外部 GCC 是明确记录的辅助工具。它可以负责预处理、汇编、链接和提供 CRT/libc，但不得替 MiniC 编译任何 C 函数。

原生预处理器、汇编器、链接器、libc 替换和完整自举属于后续独立里程碑。

## 当前实现

C 版本现在已经具备：

- Token、源码位置模型和独立 Lexer；
- 按表达式、后缀操作、语句、函数、类型、记录、typedef、全局对象、成员和常量拆分的模块化 Parser；
- 明确的 Parsed 与 Normalized AST 合约：在流水线边界验证存储所有权、类型身份、值类别、子节点早于父节点的拓扑、调用、语句、块、函数、记录、数组、别名和全局对象；Cast 规范化位于单一翻译单元中，按拓扑顺序和事务方式重建表达式 ID，并使用仅限规范化形态的 `BITCAST` 保持指针位模式；
- 带左值/右值区分、统一解析的十进制与十六进制整数常量、明确 signed/unsigned 身份、CHAR/INT 整数等级、原生 `unsigned char`、`unsigned char -> int` 的 C 整数提升，以及安全的一级 `T * -> const T *` 资格转换；继续拒绝移除 const 和不安全的嵌套指针转换；
- 词法块作用域，以及由 Program 稳定持有的局部对象；
- 整数表达式、比较、左移/右移、按位与/异或、条件、`if`、`while`、规范化的有条件和空条件 `for` 循环、跳出最内层循环的 `break`、普通赋值与复合异或赋值、求值后丢弃结果的通用表达式语句、受限的前置递增/递减更新、指针、固定与递归数组、适用于任意合适表达式结果的可重复后缀下标、仅允许所指类型为完整对象的指针算术、逗号局部声明和 const 限定局部初始化；
- 函数原型、合法前向调用、直接与嵌套调用、递归、互递归，以及 0～8 个整数寄存器参数；
- `void`、`const`、命名记录、typedef 记录字段、局部记录对象、记录指针成员查找、标量成员左值、数组成员衰变、递归数组 typedef、静态只读全局数组、带局部同名遮蔽的全局数组表达式查找和内部函数；
- RV64 字节/int/指针标量布局、共享类型大小/对齐查询、一字节数组和记录字段布局、`lbu`/`sb` 字节访问、8 位截断、`.byte` 全局表发射、一字节指针/下标缩放、调用安全栈帧、signed/unsigned 加载与运算、`sllw`/`sraw`/`srlw` 移位降低、`and`/`xor` 降低、规范化 `BITCAST` 透传降低、保证目标只求值一次的 `^=` 读改写降低、有条件和无条件循环降低、规范化的加一/减一循环更新、供 `break` 使用的最内层循环退出目标、成员基址加偏移寻址、表达式语句求值与结果丢弃、通过共享布局查询完成 2 的幂和任意聚合大小的指针/下标缩放、汇编发射和内部符号可见性；
- Debug、Release `-Werror`、ASan/UBSan、RV64/QEMU 和 GCC/MiniC 双轨差分门禁。

当前有 38 个可执行 C 程序永久运行两条流水线，并比较退出码、标准输出和标准错误。矩阵包含循环变量与循环体执行次数的隔离测试、带最内层 `break` 和跳过循环尾更新的嵌套空条件循环、`break` 跳过递减尾更新的下降循环、高位 unsigned 比较/除法/取余、原生 unsigned-char 截断/提升/布局/访问、十六进制表达式常量、指针参数和局部指针下标读写、const 局部初始化与读取、带局部同名遮蔽的静态全局数组查找、signed/unsigned 按位异或、有符号与无符号移位、按位与、带非零字段偏移的记录指针成员、通用表达式语句、覆盖非 2 的幂 12 字节行跨度的多维数组指针下标、12 字节记录和数组指针的双向加法与减法，以及通过带副作用复杂目标验证“只求值一次”的复合异或赋值。

## 第一个外部真实项目——已完成

第一个固定上游驱动项目是 `kokke/tiny-AES-c`，验收配置为 AES-128 ECB。上游原始 `aes.c`、`aes.h` 和 Unlicense 文件存放在 `tests/vendor/tiny-aes-c/upstream/`；`tests/vendor/tiny-aes-c/PROVENANCE` 记录固定上游提交和 Git Blob 身份。三份文件不为 MiniC 修改，永久门禁会在不联网下载源码的情况下重新校验其身份。

这个项目虽然叫 tiny-AES-c，但算法实现有意高度集中：主要实现文件就是 `aes.c`，`aes.h` 是公开接口，其他 C 内容主要是测试和示例。MiniC 已经在真实 `typedef unsigned char uint8_t` 下完整编译固定版本的 `aes.c`，生成 RV64 汇编，并链接成静态 RISC-V 可执行文件。

永久独立 harness 会验证全部 176 字节 AES-128 展开轮密钥、第一轮中间状态、标准 AES-128 ECB 密文，以及解密后恢复原始明文。GitHub Actions 干净检出第 556 次运行在 QEMU 下同时执行完整 GCC 参考程序和 MiniC 程序；两条流水线均退出 0，标准输出和标准错误均为空。MiniC 生成的目标文件为 26,016 字节，37 个通用差分程序也全部通过。

这已经满足首个外部项目预先声明的完成标准。AES-128 ECB 配置现已冻结为永久离线回归门禁。CBC/CTR 不属于本里程碑要求，是否扩展应作为未来独立负载选择，而不是继续拖长当前项目。

## 构建与验证

```sh
make
make check-fast
make sanitize
```

提供 RISC-V Linux 编译器和 QEMU 用户态执行器时：

```sh
make check-runtime \
  RISCV_CC=riscv64-linux-gnu-gcc \
  QEMU_RISCV64=qemu-riscv64 \
  REQUIRE_RISCV_RUNTIME=1
```

GitHub Actions 会在 Ubuntu 24.04 干净虚拟机中运行完整门禁，包括离线固定 tiny-AES AES-128 ECB 双轨执行门禁。

## 项目规则

- 真实软件决定优先级，标准和 ABI 文档决定语义。
- 当前采用模块化单体，不建立保存所有状态的万能编译器对象。
- 公共接口保持精简，子系统内部可以拆成聚焦文件。
- 只有所有权、生命周期、平台差异或多个真实实现提供证据时才增加抽象。
- 临时架构债务必须可见、范围受限，并具有具体退出条件。
- 生产、架构、构建和迁移类提交使用中英双语正文，并记录实际执行的验证。

相关文档：

- [`docs/architecture/principles.md`](docs/architecture/principles.md)
- [`docs/architecture/compiler-development-roadmap.md`](docs/architecture/compiler-development-roadmap.md)
- [`docs/standards/implementation-language.md`](docs/standards/implementation-language.md)
- [`docs/standards/validation-toolchain.md`](docs/standards/validation-toolchain.md)
- [`docs/DEVIATIONS.md`](docs/DEVIATIONS.md)
- [`docs/milestones/compiler-c3-tiny-aes-frontier.md`](docs/milestones/compiler-c3-tiny-aes-frontier.md)
