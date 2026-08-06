# tiny-AES-c external status / tiny-AES-c 外部项目状态

This directory tracks the first independently maintained upstream project used to drive MiniC.

本目录跟踪首个用于驱动 MiniC 的独立维护上游项目。

## Upstream identity / 上游身份

- Repository / 仓库：`kokke/tiny-AES-c`
- Commit / 提交：`23856752fbd139da0b8ca6e471a13d5bcc99a08d`
- License / 许可证：Unlicense (`unlicense.txt`)
- Accepted configuration / 当前验收配置：AES-128 ECB (`ECB=1`, `CBC=0`, `CTR=0`)

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

## Current accepted state / 当前已验收状态

MiniC now compiles the complete pinned AES-128 ECB core with a real target byte type:

MiniC 现在使用真实目标字节类型完整编译固定 AES-128 ECB 核心：

```c
typedef unsigned char uint8_t;
```

The accepted compiler path is:

已验收编译路径为：

```text
pinned upstream aes.c + unchanged aes.h
-> external RISC-V GCC preprocessing
-> MiniC compilation to RV64 assembly
-> external RISC-V assembly
-> non-empty target object
```

The implementation includes native `unsigned char` identity, CHAR/INT ranks, integer promotion to `int`, one-byte scalar/array/record/global/local layout, `lbu`/`sb`, 8-bit conversion and truncation, `.byte` lookup-table emission, and one-byte pointer/subscript scaling.

实现已经包含原生 `unsigned char` 身份、CHAR/INT 等级、提升到 `int` 的整数提升、一字节标量/数组/记录/全局/局部布局、`lbu`/`sb`、8 位转换和截断、`.byte` 查找表发射，以及一字节指针/下标缩放。

Clean-checkout run #532 passed:

干净检出第 532 次运行通过：

- Debug, Release `-Werror`, and ASan/UBSan host gates / 三套宿主门禁；
- focused Token, Lexer, Type, Layout, byte-access, and plain-char boundary gates / Token、Lexer、Type、Layout、字节访问和 plain-char 边界门禁；
- focused RV64/QEMU runtime gates / 聚焦 RV64/QEMU 门禁；
- thirty-six GCC/MiniC differential executable programs / 36 个 GCC/MiniC 可执行差分程序；
- `unsigned_char_layout` exits 127 in both lanes with empty output / `unsigned_char_layout` 两条流水线均退出 127 且无输出；
- MiniC-generated tiny-AES assembly produces an 18,504-byte RV64 object / MiniC tiny-AES 汇编生成 18,504 字节 RV64 目标文件；
- `AES_ECB_encrypt` and `AES_ECB_decrypt` symbols are present when target `nm` is available / 可用目标 `nm` 时确认两个 ECB 入口符号存在。

## Deliberate boundary / 明确边界

Plain `char` and signed-char semantics remain outside this capability slice. The parser accepts `unsigned char` and typedefs such as `uint8_t`; a permanent negative fixture keeps bare `char` unsupported until it receives its own complete type and target-policy decision.

plain `char` 与 signed-char 语义仍在本能力切片之外。Parser 接受 `unsigned char` 及 `uint8_t` 等 typedef；永久负例会继续拒绝 bare `char`，直到它拥有独立且完整的类型与目标策略。

## Remaining acceptance milestone / 剩余验收里程碑

Compilation and assembly of the complete core are accepted. The software itself is not frozen yet because the current harness only supplies the required `int main` and does not execute AES operations.

完整核心的编译和汇编已经验收；但软件本身尚未冻结，因为当前 harness 只提供编译器要求的 `int main`，并未实际执行 AES 运算。

The final project milestone is:

最终项目里程碑为：

1. define independent AES-128 ECB key/plaintext/ciphertext vectors / 定义独立 AES-128 ECB 密钥、明文和密文向量；
2. link the MiniC-generated core into a runnable RISC-V executable / 将 MiniC 生成的核心链接成可运行 RISC-V 程序；
3. execute encryption and decryption under QEMU / 在 QEMU 下执行加密和解密；
4. compare exit status, stdout, and stderr with a full GCC reference lane / 与完整 GCC 参考流水线比较退出码、标准输出和标准错误；
5. update the permanent probe and documentation, then freeze tiny-AES / 更新永久探针和文档后冻结 tiny-AES。

No further C syntax frontier is currently known inside the pinned AES-128 ECB core. Any failure in the execution milestone must be treated as a semantic, ABI, layout, code-generation, or harness issue and diagnosed from evidence rather than patched around in upstream source.

固定 AES-128 ECB 核心内部目前没有已知剩余 C 语法前沿。执行里程碑中的任何失败，都必须按语义、ABI、布局、代码生成或 harness 问题基于证据排查，不得通过修改上游源码绕过。
