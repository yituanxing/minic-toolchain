# 当前 MiniC Stage0→Stage1→Stage2 自举固定点实证（2026-10-17）

**最新源码的正式 B1 fixed point 已通过，不是拿历史冻结 Stage2 冒充当前版本。**

- Source/CI trigger: `main@3b6c1e335342b042f5f3c0b922f31843e6824bc9`，现有 `linux-runtime-optin-perf-suite-v1.yml` 的 **perf500 单一 Job**，无新建 workflow/job。
- [完整 CI #39040013459](https://github.com/yituanxing/minic-toolchain/actions/runs/39040013459)：**SUCCESS**，包含 Stage0、Stage1 以及真正在 QEMU 用户态运行的 Stage1→Stage2 全 47 TU fixed-point。
- Stage0 seed 的独立前置验收：[CI #39039781829](https://github.com/yituanxing/minic-toolchain/actions/runs/39039781829) **SUCCESS**，同为 47/47 Stage0 和 Stage1 `return_42`。
- **被自举的对象是当前 `bin/minic-cc` C 编译器，不是用户界面 Driver `tools/minic/main.c`**；不能声称无 GNU 工具链依赖。

## 1. 与生产源码完全一致的自举输入

现有 `tests/compiler/bootstrap/b0-sources.txt` 与 `Makefile:MINIC_SOURCES` **47/47 内容及顺序逐项相同**，包含 `src/core/core_lower_bool.c`，最后是 `tools/minic-cc/main.c`；旧 B1 的 46 TU 历史清单已经在先前的 `73e26a99` 中修正，不应再次修改。此轮新增独立清单比对，禁止误把 driver 当作编译器。

`Stage0` 采用主机 GCC 和当前真实生产 C 源编译 `minic-cc`，通过 `check-type` 和 `check-ast-contract`；随后每个目标源文件经 `riscv64-linux-gnu-gcc -E -P -std=c11 -Iinclude -Isrc` 得到实物 `.i`，使用当前 Stage0 `minic-cc -S` 输出 RV64 `.s`。严格追踪 47 份文件的来源、`.i`/Stage0 `.s` SHA，统一固定 `MINIC_LINUX_FULL_CROSSBLOCK=1`、`MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1`。

**精确 Stage0 编译器 SHA256**：`f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd`，与 2026-10-17 已认证 Linux Release 编译器一致。47/47 Stage0 源文件已成功编译。

## 2. Stage1 和 Stage2 验收

```text
当前正式源码（47 C）
  → 主机 GCC 构建 Stage0 minic-cc
  → GNU RV64 预处理 .i
  → Stage0 为 47 个源码生成 .s
  → GNU RV64 静态汇编 + 链接
  → Stage1 RV64 minic-cc （QEMU 可执行）
  → Stage1 在 qemu-riscv64 下重新编译同样的 47 个 .i
  → 47/47 与 Stage0 .s SHA **严格相同**
  → GNU 静态汇编 + 链接 Stage2 RV64 minic-cc
  → objcopy -O binary Stage1/Stage2 可加载镜像 **逐字节相同**
  → Stage2 编译 return_42；与 Stage1 汇编完全相同
  → 生成程序在 qemu-riscv64 返回退出码 42
```

| 固定点条件 | 结果 |
| --- | --- |
| Source manifest 与生产 Makefile | **47/47 PASS** |
| Stage0 host 编译器 + 单元契约 | **PASS** |
| Stage0 原生 C TU `.i → .s` | **47/47 PASS** |
| Stage1 RV64 静态 ELF 链接 | **PASS** |
| Stage1 QEMU `return_42` | **42 / PASS** |
| Stage1 在 QEMU 下重编自己 | **47/47 PASS** |
| Stage1 `.s` 与 Stage0 `.s` | **47/47 字节相同** |
| Stage2 RV64 静态链接 | **PASS** |
| Stage1 与 Stage2 可加载镜像 | **逐字节相同 / PASS** |
| Stage2 在 QEMU 下编译 `return_42` | **与 Stage1 输出字节相同** |
| Stage2 `return_42` 运行 | **42 / PASS** |
| Stage2 编译真实中型程序与 Linux | **未在本轮运行；不得声称 PASS** |

本轮 Stage1 的 47 个 QEMU 编译使用同 Runner 内 **4 个 worker**，每个进程各自编译真实 `.i`，记录每个 TU 耗时。性能仅用于确保能在可界定时长内完成自举，不是 MiniC/Stage0 和 QEMU/Stage1 的同架构 A/B。
- Stage1 ELF SHA256：`b13a80ec80104c50eb219fad8359f103583fc20080521065836f5029a7ea2884`；
- Stage2 ELF SHA256：`17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d`；
- 两者 loadable binary SHA256：`48c10ab3aca7f80cbf534be8d00cdfda3d49bba7f64f0af1a2933348c8846d33`。

**只要求并证实 loadable image 完全相同，原始 ELF 可因非可加载的链接元数据不同。** 不把原始 ELF 不同当作编译器不自举，更不能无验证地说 raw ELF 相同。

## 3. 复用历史成果，禁止伪造“新建”或覆盖

此轮继续沿用 `.github/workflows-disabled/compiler-bootstrap-b1-sharded.yml` 的旧固定点判定规则，而不是引入全新验证概念；区别是把此前已修正的 47 个当前编译器目标输入放进 `tools/ci/minic-bootstrap-current-b1-seed-v1.py` 和 `tools/ci/minic-bootstrap-current-b1-stage2-v1.py`，在已经启用的一个 opt-in perf500 Runner 内完成预热、整体验证和失败诊断。旧 workflow 未删除、未激活；没有改任何生产 `src/`。

有两个源实验入口（不依赖 GitHub Action 的路径）：

```bash
python3 tools/ci/minic-bootstrap-current-b1-seed-v1.py \
  --work build/bootstrap-current-b1
python3 tools/ci/minic-bootstrap-current-b1-stage2-v1.py \
  --work build/bootstrap-current-b1 --workers 4
```

依赖：Linux 主机 GCC、make、Python 3、GNU `riscv64-linux-gnu-gcc` 及其静态 CRT/libc、`riscv64-linux-gnu-objcopy`、`qemu-riscv64`；在非 Linux 主机上不能假定现有环境支持。

CI 保留两个**实际实验 Artifact**：`minic-current-bootstrap-b1-seed-v1`（Stage1 ELF、真实输入 manifest、源 `.i`、日志、hash）与 `minic-current-bootstrap-b1-stage2-v1`（Stage2 ELF、重编译逐 TU 诊断、Stage2 fixed point 证明）。7 天保留策略内可直接复用，不应为了复查 Stage2 输出重新预处理内核 2064 文件。

## 4. 自举 Stage2 真实程序后续验证（已完成，2026-10-17）

继当前 47/47 自举固定点之后，这次**直接下载、SHA 确认并使用 #39040013459 的 Stage2 RV64 ELF**（sha256 `17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d`），在 `qemu-riscv64` 中运行，绝不把 x86 Stage0/GCC 的运行结果冒称为自举 Stage2。GitHub `git diff` 检查 `src/`、`include/`、`tools/minic-cc/`、`b0-sources.txt` 在 certified self-host source revision `3b6c1e33` 之后未变，确保 Artifact 源码的有效性。不改编译器生产源码，不新增 workflow/job；仍复用唯一 opt-in `perf500` 工作流。

**[Stage2 C0 + 全套程序差分 #39041763260](https://github.com/yituanxing/minic-toolchain/actions/runs/39041763260) SUCCESS**：

- 原有 `tests/compiler/c0/run-runtime.sh`：**全项 PASS**，包括最后的 RV64D double 返回 ABI；打印 `BOOTSTRAP_STAGE2_C0_RUNTIME=PASS`。测试中的真正编译器为 Stage2 的 `qemu-riscv64` wrapper，并非 GCC Host。
- `tests/programs/c0/manifest.txt`：**79/79** 各自的 GCC PP `.i` 用相同 Release `Stage0` 和自举 `Stage2` 两者分别生成汇编，**79/79 .s SHA 全同**；**79/79 自举 Stage2 产出的 ELF 能执行**。
- GNU 程序比较：**77/79 退出码、stdout、stderr 完全一致**；一项 **真实差异**：`predefined_func_name` 自举 MiniC 返回 0，GNU GCC 返回 6。它检查非标准 `__FUNCTION__` 和 C99 `__func__` 的对象指针同一性：MiniC 在相同函数中使用一个唯一静态对象（其 C0 正向/负向 contract 已单独验证），GNU 的这两个数组不是同一地址。并不能掩盖为 79/79 GNU 等价，也不能因为二者语义不同就说 Stage2 自举失败，原始差分仍保留。
- 一项 GNU oracle **无法构建/无运行结果**：`post_type_declaration_specifiers.c` 的 `Word inline helper` 读取 static 变量，在 `riscv64-linux-gnu-gcc -std=c11 -O0 -static` 下链接出现 undefined reference to `helper`；Stage0、Stage2 `.s` 仍相同，Stage2 产物 ELF 可执行并返回 0。列为 `GNU LINK_FAILED`，**不是** GNU/Stage2 runtime match，更不是 Stage2 的故障。原测试与 manifest 没修改。
- 一次首次跑 #39041095371 失败于原 GCC 比较脚本的 `predefined_func_name` 真差异；再次 #39041448311 证明了 `inline` GNU oracle 编译失败。后续只对 CI 诊断脚本增加**原始失败的清晰分类**，保留每一项测量和 GNU 原测试不变。相应 json artifact `minic-current-bootstrap-stage2-realprograms-v1` 中 `parity/program-parity.json` 保存 79 条逐项日志与 SHA。

**[Stage2 真实开源库 #39042040646](https://github.com/yituanxing/minic-toolchain/actions/runs/39042040646) SUCCESS**：

| 现有正式入口，真实 Stage2 QEMU Wrapper | 结果 |
| --- | --- |
| `tests/external/tiny-aes-c/probe.sh`，AES-128 ECB 真实向量 | **PASS**；退出 0，输出 0，运行二进制 71,776B |
| `tests/external/cjson/probe.sh`，固定 cJSON 1.7.19，parse-object-array-print-roundtrip | **PASS**；退出 0，二进制 774,776B |
| `tests/external/cjson/final_differential.sh`，上游 cJSON `test.c` 完整差分 | **PASS**；GNU vs Stage2 编译 cJSON 的程序退出均 0、stdout/stderr **逐字节相同**；stdout 873B |

真实项目使用的 C 编译器确实是 Stage2，并在 QEMU 中运行，但预处理和汇编/链接、CRT/libc 仍由 GNU 提供。**还没有让自举 Stage2 编译 Linux 内核对象，更没有使用 Stage2 构建 Linux 并 QEMU P1**；这应是后续下一道独立验收，不得把既有的宿主 Stage0 Linux 2064 PASS 计作 Stage2 Linux PASS。

## 4. 这次不再继续投入没有效果的常规性能优化

同日已有七 TU 严格 A/B 确认 Core Reentry 稀疏语句遍历只有 **1.00674×**，已按 1.04× 门槛 STOP；新一轮 CPU 证据没有给出比自举更有价值、尚未尝试的确定性大收益算法。性能迭代先冻结，切换到当前代码的自举验证是合理的；不是宣称编译器已经达到理论最优。

下一实际验收重点：**使用本轮 Stage2** 运行历史 `tests/compiler/c0/run-runtime.sh` 与 `tests/programs/c0/run.sh`，再推进 Lua/cJSON 等真实程序。随后单独考虑 Stage2 作为 Linux 编译器的可信正确性门槛，不因已通过 Stage0 MiniC 编译 Linux 就宣布 Stage2 已经能编译和运行完整 Linux。正式 GNU PP/AS 的完整 Linux 2064 + QEMU P1 已在 `#39036801157` 等运行中通过，但其编译器不是本轮 Stage2 产物，因此两份证据必须分开。
