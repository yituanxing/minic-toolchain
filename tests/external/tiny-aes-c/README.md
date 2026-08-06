# tiny-AES-c external status / tiny-AES-c 外部项目状态

This directory tracks the first independently maintained upstream project used to drive MiniC.

本目录跟踪首个用于驱动 MiniC 的独立维护上游项目。

## Upstream identity / 上游身份

- Repository / 仓库：`kokke/tiny-AES-c`
- Commit / 提交：`23856752fbd139da0b8ca6e471a13d5bcc99a08d`
- License / 许可证：Unlicense (`unlicense.txt`)
- Accepted configuration / 已验收配置：AES-128 ECB (`ECB=1`, `CBC=0`, `CTR=0`)

Pinned Git blob identities / 固定 Git Blob：

| File | Git blob SHA-1 |
|---|---|
| `aes.c` | `4481f7b24ec964019d38669842913fd571d28ba3` |
| `aes.h` | `b29b6683549632676ec11c06eb86efd02964db57` |
| `unlicense.txt` | `68a49daad8ff7e35068f2b7a97d643aab440eaec` |

## Project shape / 项目形态

The implementation is intentionally tiny and concentrated. `aes.c` is the principal cryptographic implementation file, `aes.h` is its public interface, and the remaining C material in the upstream repository is primarily tests or examples. Progress through `aes.c` therefore represents progress through essentially the complete AES core rather than merely the first of many independent implementation files.

该项目有意保持极小且高度集中。`aes.c` 是主要密码算法实现文件，`aes.h` 是公开接口；上游其余 C 内容主要用于测试或示例。因此，越过 `aes.c` 基本等同于越过完整 AES 核心，而不是只处理许多独立实现文件中的第一个。

## Rules / 规则

1. Downloaded upstream files are not edited for MiniC / 不为 MiniC 修改下载的上游文件。
2. The RISC-V GCC preprocessor remains outside the compiler-under-test boundary / RISC-V GCC 预处理器仍在被测编译器边界之外。
3. Target-environment shim headers may define standard integer names, but accepted semantics must match the real target width / 目标环境 shim 可以定义标准整数名称，但验收语义必须符合真实目标宽度。
4. The probe verifies every upstream Git blob before preprocessing / 探针在预处理前校验每个上游 Git Blob。
5. Production support must have permanent focused positive and negative coverage / 生产能力必须具有永久聚焦正负门禁。
6. Completion requires a GCC/MiniC differential AES-128 ECB test-vector execution without MiniC-specific upstream patches / 完成要求在无 MiniC 专用上游补丁的前提下通过 GCC/MiniC AES-128 ECB 标准向量执行差分。

## Completed compiler path / 已完成编译路径

MiniC uses a real target byte type:

MiniC 使用真实目标字节类型：

```c
typedef unsigned char uint8_t;
```

The accepted path is:

已验收路径为：

```text
pinned upstream aes.c + unchanged aes.h + independent vector harness
-> external RISC-V GCC preprocessing
-> MiniC compilation to RV64 assembly
-> external RISC-V assembly and static linking
-> QEMU RISC-V execution
-> comparison with a full GCC reference executable
```

The implementation includes native `unsigned char`, CHAR/INT ranks, integer promotion, one-byte scalar/array/record/global/local layout, `lbu`/`sb`, 8-bit conversion and truncation, `.byte` lookup-table emission, one-byte pointer/subscript scaling, safe first-level const-pointer qualification conversion, and canonical decimal/hexadecimal expression constants.

实现包含原生 `unsigned char`、CHAR/INT 等级、整数提升、一字节标量/数组/记录/全局/局部布局、`lbu`/`sb`、8 位转换和截断、`.byte` 查找表发射、一字节指针/下标缩放、安全一级 const 指针资格转换，以及统一的十进制/十六进制表达式常量解析。

## Execution acceptance / 执行验收

The independent libc-free harness uses the standard AES-128 key, plaintext, and ciphertext also used by the pinned upstream tests. It verifies:

独立无 libc harness 使用固定上游测试采用的标准 AES-128 密钥、明文和密文，并验证：

- all 176 expanded round-key bytes / 全部 176 字节展开轮密钥；
- initial AddRoundKey and first-round SubBytes, ShiftRows, MixColumns, and AddRoundKey states / 初始 AddRoundKey 与第一轮各阶段状态；
- the final standard AES-128 ECB ciphertext / 最终标准 AES-128 ECB 密文；
- decryption back to the original plaintext / 解密恢复原始明文。

GitHub Actions clean-checkout run #556 passed:

GitHub Actions 干净检出第 556 次运行通过：

- Debug, Release `-Werror`, and ASan/UBSan host gates / 三套宿主门禁；
- focused pointer-qualification and hexadecimal-expression positive/negative gates / 指针资格转换和十六进制表达式聚焦正负门禁；
- focused RV64/QEMU runtime gates / 聚焦 RV64/QEMU 门禁；
- thirty-seven GCC/MiniC differential executable programs / 37 个 GCC/MiniC 可执行差分程序；
- `hexadecimal_expression`: exit 77 in both lanes / `hexadecimal_expression` 两条流水线均退出 77；
- GCC and MiniC AES vector executables: exit 0, empty stdout, empty stderr / GCC 与 MiniC AES 向量程序均退出 0，标准输出和错误均为空；
- MiniC-generated object size: 26,016 bytes / MiniC 目标文件大小为 26,016 字节；
- `AES_ECB_encrypt` and `AES_ECB_decrypt` symbols present / ECB 加解密入口符号存在。

## Defects exposed and generalized / 暴露并通用化修复的问题

Execution exposed two compiler-wide issues rather than tiny-AES-specific source problems:

执行阶段暴露的是两项编译器通用问题，而非 tiny-AES 专用源码问题：

1. valid first-level `T * -> const T *` argument conversion was rejected; the type system now accepts qualification addition while rejecting const removal and unsafe `T ** -> const T **` conversion / 原先错误拒绝一级 `T * -> const T *` 实参转换；类型系统现允许增加限定，同时拒绝移除 const 和不安全的嵌套转换；
2. expression hexadecimal constants used a duplicate decimal-only parser, so `0x4d` became 7292 and indexed beyond the S-box; expressions now reuse the canonical decimal/hexadecimal parser, with focused and differential coverage / 表达式十六进制常量曾走重复的十进制专用解析器，使 `0x4d` 变成 7292 并越界访问 S-box；现已统一复用正确解析器并加入永久门禁。

No upstream source patch was used.

未使用任何上游源码补丁。

## Frozen status / 冻结状态

The declared completion criteria for the pinned AES-128 ECB workload are satisfied. This project is now frozen as a permanent regression gate rather than an active syntax frontier.

固定 AES-128 ECB 负载已满足预先声明的全部完成标准。该项目现冻结为永久回归门禁，不再是活动语法前沿。

Plain `char`, signed-char policy, CBC, and CTR are not required for this accepted configuration. They may be selected later only as separate language or workload milestones.

plain `char`、signed-char 策略、CBC 和 CTR 不属于当前已验收配置的要求；未来若需要，应作为独立语言或负载里程碑选择。
