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
- 带左值/右值区分、明确 signed/unsigned 整数身份，以及同等级整数转换的类型化表达式；
- 词法块作用域，以及由 Program 稳定持有的局部对象；
- 整数表达式、比较、左移/右移、按位与/异或、条件、`if`、`while`、规范化的受限 `for` 循环、普通赋值与复合异或赋值、求值后丢弃结果的通用表达式语句、前置递增、指针、固定与递归数组、适用于任意合适表达式结果的可重复后缀下标、指针算术、逗号局部声明和 const 限定局部初始化；
- 函数原型、合法前向调用、直接与嵌套调用、递归、互递归，以及 0～8 个整数寄存器参数；
- `void`、`const`、命名记录、typedef 记录字段、局部记录对象、记录指针成员查找、标量成员左值、数组成员衰变、递归数组 typedef、静态只读全局数组、带局部同名遮蔽的全局数组表达式查找和内部函数；
- RV64 标量/数组/记录对象布局、共享类型大小/对齐查询、字段偏移与聚合对齐、调用安全栈帧、signed/unsigned 加载与运算、`sllw`/`sraw`/`srlw` 移位降低、`and`/`xor` 降低、保证目标只求值一次的 `^=` 读改写降低、成员基址加偏移寻址、表达式语句求值与结果丢弃、2 的幂与任意聚合下标跨度缩放、汇编发射和内部符号可见性；
- Debug、Release `-Werror`、ASan/UBSan、RV64/QEMU 和 GCC/MiniC 双轨差分门禁。

当前有 32 个可执行 C 程序永久运行两条流水线，并比较退出码、标准输出和标准错误。矩阵包含循环变量与循环体执行次数的隔离测试、高位 unsigned 比较/除法/取余、指针参数和局部指针下标读写、const 局部初始化与读取、带局部同名遮蔽的静态全局数组查找、signed/unsigned 按位异或、有符号与无符号移位、按位与、带非零字段偏移的记录指针成员、通用表达式语句、覆盖非 2 的幂 12 字节行跨度的多维数组指针下标，以及通过带副作用复杂目标验证“只求值一次”的复合异或赋值。

## 第一个外部真实项目

第一个固定上游驱动项目是 `kokke/tiny-AES-c`，当前配置为 AES-128 ECB。CI 会按固定 Git Blob 校验上游源码，不允许为 MiniC 修改上游文件。

编译器已经越过上游的声明、typedef 数组、静态查找表、内部函数、`void` 函数定义、unsigned 声明列表、`KeyExpansion` 的第一条循环、指针参数下标、const 限定局部初始化、静态全局 `sbox` 表的表达式引用、按位异或、typedef 记录字段、记录布局、首次 `ctx->RoundKey` 指针成员访问、独立的 `KeyExpansion(ctx->RoundKey, key);` 表达式语句、`(*state)[i][j]` 的可重复后缀下标、`AddRoundKey` 中的复合异或赋值，以及 `xtime` 中完整的左移、右移和按位与表达式。当前精确前沿是 `Cipher` 内 `for (round = 1; ; ++round)` 的空循环条件。

这个目标 shim 只是阶段性验证脚手架，并不代表已经实现 `uint8_t`。真正的一字节类型身份、对象布局、加载/存储、下标缩放和整数转换仍需独立实现；在这些能力完成前，不能把 AES 执行结果视为正确。

该项目尚未完成。完成标准是：固定版本的 AES-128 ECB 核心和测试向量驱动，在不加入 MiniC 专用源码补丁的前提下，通过 GCC/MiniC 双轨差分。

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

GitHub Actions 会在 Ubuntu 24.04 干净虚拟机中运行完整门禁，包括固定的 tiny-AES 前沿检查。

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
