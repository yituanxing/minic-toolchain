# External Validation Toolchain / 外部验证工具链

## 1. Purpose / 目的

MiniC's compiler-first track replaces only the C compilation stage. External tools remain explicit, audited validation dependencies until later milestones deliberately replace them.

MiniC 的“编译器优先”主线当前只替换 C 编译阶段。其他工具在后续里程碑明确接管前，继续作为显式、可审计的验证依赖。

The current pipeline is:

当前流水线为：

```text
C source
  -> external RISC-V GCC preprocessor
  -> MiniC compiler (.i -> .s)
  -> external RISC-V assembler and linker driver
  -> static RISC-V ELF
  -> QEMU RISC-V user-mode execution
```

External GCC **MUST NOT** compile a C function on MiniC's behalf. Its permitted target-side roles in this phase are preprocessing, assembling MiniC-generated assembly, linking, and supplying the external musl/CRT/runtime environment.

外部 GCC **不得**替 MiniC 编译任何 C 函数。当前阶段允许它承担的目标侧职责仅包括预处理、汇编 MiniC 生成的汇编代码、链接，以及提供外部 musl、CRT 和运行时环境。

## 2. Host implementation compiler / 宿主实现编译器

`CC` builds the MiniC implementation itself on the host.

`CC` 用于在宿主机上构建 MiniC 本身。

Validated environment on 2026-08-04:

2026-08-04 已验证环境：

```text
CC version:     cc (Debian 14.2.0-19) 14.2.0
Host triplet:   x86_64-linux-gnu
Language mode:  ISO C11
```

The host compiler is not the target oracle and does not assemble or link RISC-V products in the formal runtime gate.

宿主编译器不是目标侧 Oracle，也不参与正式运行门禁中的 RISC-V 汇编或链接。

## 3. Auxiliary RISC-V musl toolchain / 辅助 RISC-V musl 工具链

Pinned validation archive:

固定验证归档：

```text
Archive:  riscv64-lp64d--musl--stable-2025.08-1.tar.xz
Size:     93,100,104 bytes
SHA-256:  2c5155ce133c9c8dddde8f69b0715aa07e0520d99b1fd0131d915357c6fbce39
```

Validated tools extracted from that archive:

从该归档中验证的工具：

```text
GCC driver:  riscv64-buildroot-linux-musl-gcc
GCC version: riscv64-buildroot-linux-musl-gcc.br_real
             (Buildroot 2021.11-18033-g83947c7bb6) 14.3.0
Target:      riscv64-buildroot-linux-musl
GNU ld:      GNU Binutils 2.43.1
libgcc:      lib/gcc/riscv64-buildroot-linux-musl/14.3.0/libgcc.a
Sysroot:     riscv64-buildroot-linux-musl/sysroot
```

The repository refers to this driver through `RISCV_CC`. Absolute extraction paths are environment-specific and are not committed.

仓库通过 `RISCV_CC` 引用该驱动。解压后的绝对路径属于具体执行环境，不写入仓库。

## 4. QEMU user-mode executor / QEMU 用户态执行器

Artifact used for the current gate:

当前门禁实际使用的文件：

```text
File:      qemu-riscv64-static(1)
Size:      4,813,480 bytes
SHA-256:   90836a82c85c5636b29e106583f6be3b9df263a860dbd5d39946544f105d83aa
Format:    x86-64 static PIE executable, stripped
Version:   qemu-riscv64 11.0.1
```

The repository refers to this executable through `QEMU_RISCV64`.

仓库通过 `QEMU_RISCV64` 引用该程序。

Historical V223/V224 provenance documents name `qemu-riscv64-static(2)`. The current gate records the exact `(1)` artifact actually recovered and executed; byte identity between the two names is not assumed without a hash comparison.

历史 V223/V224 来源记录使用名称 `qemu-riscv64-static(2)`。当前门禁记录的是本次实际恢复并执行的 `(1)` 文件；在没有校验值对比前，不假定两个名称对应的文件字节完全一致。

## 5. Required command boundary / 必须保持的命令边界

```sh
"$RISCV_CC" -E -P -x c input.c -o input.i
minic -S input.i -o input.s
"$RISCV_CC" -static input.s -o input.elf
"$QEMU_RISCV64" input.elf
```

Using the target compiler for preprocessing preserves the target predefined macros and target header environment. The normal fast gate may use the host preprocessor for target-independent microtests, but formal executable acceptance uses `RISCV_CC` for all external target stages.

使用目标编译器进行预处理，可以保留目标架构预定义宏和目标头文件环境。普通快速门禁可对与目标无关的微型测试使用宿主预处理器；正式可执行验收则由 `RISCV_CC` 承担全部外部目标阶段。

## 6. C0 executable acceptance / C0 可执行验收

Validated on 2026-08-04 against branch `compiler/c0-external-toolchain`, C0 commit `f001ae6`:

2026-08-04 在 `compiler/c0-external-toolchain` 分支、C0 提交 `f001ae6` 上完成验证：

```text
empty_main  expected 0   actual 0   PASS
return_0    expected 0   actual 0   PASS
return_42   expected 42  actual 42  PASS
```

All three outputs were RISC-V 64-bit, double-float ABI, statically linked ELF executables and ran through QEMU user mode.

三个产物均为 RISC-V 64 位、双精度浮点 ABI、静态链接 ELF，并已通过 QEMU 用户态运行。

## 7. Reproduction / 复现

```sh
make check-c0-runtime \
  RISCV_CC=/path/to/riscv64-buildroot-linux-musl-gcc \
  QEMU_RISCV64=/path/to/qemu-riscv64-static \
  REQUIRE_RISCV_RUNTIME=1
```

`REQUIRE_RISCV_RUNTIME=1` converts a missing external tool from `SKIP` into a test failure and should be enabled in environments that provide the pinned tools.

在已经提供固定工具的环境中，应设置 `REQUIRE_RISCV_RUNTIME=1`，使外部工具缺失从 `SKIP` 转为测试失败。
