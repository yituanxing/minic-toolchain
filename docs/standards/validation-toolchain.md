# External Validation Toolchain / 外部验证工具链

## 1. Purpose and compiler boundary / 目的与编译器边界

MiniC's compiler-first track replaces only the C compilation stage. External tools remain explicit validation dependencies until later milestones deliberately replace preprocessing, assembly, linking, CRT/libc, or execution.

MiniC 的“编译器优先”主线当前只替换 C 编译阶段。预处理、汇编、链接、CRT/libc 或执行环境在后续里程碑明确接管前，继续作为显式验证依赖。

```text
C source
  -> external RISC-V GCC preprocessing
  -> MiniC compiler (.i -> .s)
  -> external RISC-V assembly and linking
  -> static RISC-V ELF
  -> QEMU RISC-V user-mode execution
```

External GCC **MUST NOT** compile a C function on MiniC's behalf. Its permitted roles are target preprocessing, assembling MiniC-generated assembly, linking, and supplying the external target runtime.

外部 GCC **不得**替 MiniC 编译任何 C 函数。当前允许的职责是目标预处理、汇编 MiniC 生成的汇编、链接和提供外部目标运行时。

The repository maintains two distinct validation profiles. They are intentionally not described as one interchangeable pinned toolchain.

仓库维护两套用途不同的验证配置，不能把它们混写成一套可互换的固定工具链。

## 2. Profile A: Ubuntu 24.04 CI reference / 配置 A：Ubuntu 24.04 CI 参考环境

This is the active GitHub Actions clean-checkout profile.

这是 GitHub Actions 当前实际使用的干净检出配置。

### Environment / 环境

```text
Runner image:  ubuntu-24.04
Target driver: riscv64-linux-gnu-gcc
Assembler/linker/objdump prefix: riscv64-linux-gnu-
Executor:      qemu-riscv64
Target libc:   Ubuntu RISC-V GNU libc cross environment
```

Requested package names are reviewed in:

软件包名称清单位于：

```text
tools/ci/ubuntu-24.04-packages.txt
```

The current list requests:

当前清单请求：

```text
gcc-riscv64-linux-gnu
libc6-dev-riscv64-cross
qemu-user
```

APT repositories and Ubuntu runner images are mutable. Therefore this profile is **version-reported**, not falsely described as byte-pinned. `tools/ci/verify-validation-tools.sh` verifies the required commands and prints the resolved GCC, Binutils, QEMU, and installed package versions in every complete run.

APT 仓库和 Ubuntu Runner 镜像会变化，因此该配置属于**运行时记录版本**，不能伪称逐字节固定。`tools/ci/verify-validation-tools.sh` 会在每次完整运行中校验必要命令，并输出实际 GCC、Binutils、QEMU 和已安装软件包版本。

For example, the clean-checkout environment observed on 2026-08-06 before this profile split reported:

例如，2026-08-06 在本次配置拆分前观察到的干净检出环境为：

```text
riscv64-linux-gnu-gcc: Ubuntu 13.3.0-6ubuntu2~24.04.1
qemu-riscv64:          8.2.2 (Ubuntu package revision may vary)
```

These values are historical evidence, not constraints on future APT resolution. The run log is the source of truth for the exact resolved versions used by a particular CI result.

这些值只是历史证据，不是未来 APT 解析必须满足的固定约束。某次 CI 实际使用的精确版本以该次运行日志为准。

### Cache semantics / 缓存语义

GitHub Actions caches downloaded `.deb` archives to reduce repeated network and package-download cost. The cache key includes the operating system, architecture, Ubuntu release identity, and the hash of `tools/ci/ubuntu-24.04-packages.txt`.

GitHub Actions 会缓存下载的 `.deb` 归档，以减少重复联网和下载成本。缓存 Key 包含操作系统、架构、Ubuntu 发行版标识以及 `tools/ci/ubuntu-24.04-packages.txt` 的哈希。

The cache is an acceleration layer, **not provenance and not a lockfile**. After either cache restoration or fresh APT installation, the same verification script checks and reports the installed profile.

缓存只是加速层，**不是来源凭据，也不是锁文件**。无论命中缓存还是重新通过 APT 安装，随后都会执行同一验证脚本检查并记录实际配置。

### CI reproduction / CI 配置复现

On a compatible Ubuntu 24.04 environment:

在兼容的 Ubuntu 24.04 环境中：

```sh
sudo apt-get update
sudo xargs -a tools/ci/ubuntu-24.04-packages.txt \
  apt-get install -y --no-install-recommends
sh tools/ci/verify-validation-tools.sh

make check-runtime \
  RISCV_CC=riscv64-linux-gnu-gcc \
  QEMU_RISCV64=qemu-riscv64 \
  REQUIRE_RISCV_RUNTIME=1
```

Comment lines in the package manifest must be filtered when reproducing manually on systems where `xargs` does not ignore them. The GitHub gate parses the manifest itself and ignores blank/comment lines.

在 `xargs` 不会忽略注释行的系统中手动复现时，应先过滤软件包清单的注释。GitHub 门禁由脚本自行解析清单，并忽略空行和注释行。

## 3. Profile B: pinned archival local profile / 配置 B：固定归档本地环境

This profile preserves the previously audited local artifacts for historical reproduction and offline work. It is not the current GitHub Actions profile.

该配置保留此前审计过的本地归档，用于历史复现和离线工作；它不是当前 GitHub Actions 配置。

### RISC-V musl archive / RISC-V musl 归档

```text
Archive:  riscv64-lp64d--musl--stable-2025.08-1.tar.xz
Size:     93,100,104 bytes
SHA-256:  2c5155ce133c9c8dddde8f69b0715aa07e0520d99b1fd0131d915357c6fbce39

Driver:   riscv64-buildroot-linux-musl-gcc
GCC:      Buildroot 14.3.0
Target:   riscv64-buildroot-linux-musl
Binutils: 2.43.1
```

### QEMU archival executor / QEMU 归档执行器

```text
File:      qemu-riscv64-static(1)
Size:      4,813,480 bytes
SHA-256:   90836a82c85c5636b29e106583f6be3b9df263a860dbd5d39946544f105d83aa
Format:    x86-64 static PIE executable, stripped
Version:   qemu-riscv64 11.0.1
```

Absolute extraction paths are environment-specific and are not committed. The tool binaries also remain outside ordinary Git to avoid repository-history bloat, host/architecture coupling, and repeated clone cost.

解压后的绝对路径属于具体环境，不写入仓库。工具二进制也继续位于普通 Git 之外，避免仓库历史膨胀、宿主/架构耦合和每次克隆成本。

Historical records also mention `qemu-riscv64-static(2)`. Byte identity between differently named files must not be assumed without a checksum comparison.

历史记录中还出现过 `qemu-riscv64-static(2)`；不同名称文件在未比较校验值前不得假定字节一致。

### Archival reproduction / 归档配置复现

```sh
make check-runtime \
  RISCV_CC=/path/to/riscv64-buildroot-linux-musl-gcc \
  QEMU_RISCV64=/path/to/qemu-riscv64-static \
  REQUIRE_RISCV_RUNTIME=1
```

This profile may produce different ELF/runtime details from the Ubuntu GNU-libc profile. Acceptance is based on each declared profile completing the MiniC/GCC differential contract, not on byte-identical ELF output across the two profiles.

该配置与 Ubuntu GNU-libc 配置可能产生不同的 ELF 或运行时细节。验收依据是各自声明的配置完成 MiniC/GCC 差分契约，而不是两套配置生成逐字节一致的 ELF。

## 4. Required command boundary / 必须保持的命令边界

```sh
"$RISCV_CC" -E -P -x c input.c -o input.i
minic -S input.i -o input.s
"$RISCV_CC" -static input.s -o input.elf
"$QEMU_RISCV64" input.elf
```

Using the target compiler for preprocessing preserves target predefined macros and headers. Target-independent host microtests may use the host preprocessor, but formal executable acceptance uses `RISCV_CC` for every external target stage.

使用目标编译器预处理可以保留目标预定义宏和头文件。与目标无关的宿主微型测试可以使用宿主预处理器；正式可执行验收则由 `RISCV_CC` 承担全部外部目标阶段。

`REQUIRE_RISCV_RUNTIME=1` converts missing tools from `SKIP` into failure and is mandatory in declared validation environments.

`REQUIRE_RISCV_RUNTIME=1` 会把缺失工具从 `SKIP` 转为失败，在正式验证环境中必须启用。

## 5. Host implementation compiler / 宿主实现编译器

`CC` builds MiniC itself on the host. It is not the target oracle and does not assemble or link formal RISC-V products. The exact host compiler version belongs to the execution profile and should be recorded in CI or local validation logs rather than inferred from old documentation.

`CC` 用于在宿主机上构建 MiniC 本身。它不是目标 Oracle，也不参与正式 RISC-V 产物的汇编和链接。宿主编译器精确版本属于具体执行配置，应记录在 CI 或本地验证日志中，而不是从旧文档推断。

## 6. Change policy / 变更策略

- Changing package names requires updating `tools/ci/ubuntu-24.04-packages.txt` in review / 修改软件包名称必须在审查中更新清单；
- the cache key follows the manifest hash automatically / 缓存 Key 自动跟随清单哈希；
- every CI run records resolved versions / 每次 CI 记录实际解析版本；
- replacing archival artifacts requires new size/checksum/version evidence / 替换归档工具必须记录新的大小、校验值和版本证据；
- large tool binaries remain external to ordinary Git / 大型工具二进制继续位于普通 Git 之外；
- any future OCI image or release asset must use a pinned digest/checksum and remain a separate infrastructure decision / 未来若使用 OCI 镜像或 Release Asset，必须固定摘要/校验值，并作为独立基础设施决策处理。
