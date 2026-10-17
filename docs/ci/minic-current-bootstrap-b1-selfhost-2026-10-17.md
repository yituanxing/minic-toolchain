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

## 5. 当前 Stage2 自举扩展到八个原生工具——首次真正联合运行（2026-10-17）

**编译阶段 [#39042731803](https://github.com/yituanxing/minic-toolchain/actions/runs/39042731803) SUCCESS**：直接复用 #39040013459 已通过 47-TU 固定点的真实 RV64 Stage2 `minic-cc`（SHA256 `17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d`），**不是**重建另一台 host 编译器冒充 Stage2。通过 `Makefile` 的现有 `*_SOURCES` 成员以及真实 MiniELF 依赖组织并去重，共 **30 个不重复的真实 C 翻译单元**，全部由 GNU RISCV GCC **只做预处理**产生同一份 `.i`，再分别由 host Stage0 和 QEMU 中的 Stage2 生成 `.s`：**30/30 汇编 SHA 完全一致**。Stage2 产出的 30 TU 经 GNU AS/静态 GNU 链接成功生成 **8 个真正的 RV64 ELF**，逐一保存 SHA256：
- `minic-cpp`（MiniPP）
- `minic-as`（MiniAS）
- `minic-ar`（MiniAR）
- `minic-ld`（MiniLD）
- `minic-objcopy`
- `minic-nm`
- `minic-strip`
- `minic`（前端 Driver）

本次 **不改变生产 C 实现**，也不激活历史已归档 workflow；全部通过现有 `perf500` 工作流的单一 Job，并复用原来的 Stage2 Artifact。

**实际运行 [#39043906455](https://github.com/yituanxing/minic-toolchain/actions/runs/39043906455) SUCCESS**：再次下载上述 Stage2 编译出的原生工具 ELF（严格对照 artifact `summary.json` 中的 8 个 SHA），每个工具以 `qemu-riscv64` 执行。五套未改动的**现有正式功能测试**全部真实通过：`tests/preprocessor/run-a0.sh`（MiniPP→GNU -E 差分），`tests/assembler/run-a0.sh`（MiniAS 汇编/重定位），`tests/archiver/run-a0.sh`（MiniAR GNU byte-exact archive），`tests/linker/run-a0.sh`（Stage2-built MiniAS 对象→Stage2-built MiniLD `-r` 链接），`tests/objcopy/run-a0.sh`（MiniObjcopy→GNU objcopy binary 差分）。

独立**端到端无 GNU AS/LD 的最小程序**证明：
```
GCC -E -P               （仅预处理固定 C 输入）
     ↓
QEMU / Stage2 MiniC      生成 main() { return 42; } 的 RV64 .s
     ↓
QEMU / Stage2 MiniAS     将 main.s + _start.s 汇编成 RV64 ELF .o
     ↓
QEMU / Stage2 MiniLD     -static -e _start 生成最终 RV64 ELF ET_EXEC
     ↓
qemu-riscv64             Linux exit syscall 93，实测退出码 42 ✅
```
CI 原样输出：`BOOTSTRAP_NATIVE_MINI_CHAIN=PASS Stage2_MiniC=true Stage2_MiniAS=true Stage2_MiniLD=true runtime_exit=42 GNU_AS_LD=false` 和 `BOOTSTRAP_STAGE2_NATIVE_RUNTIME=PASS tools=8 suites=5 runtime_chain=42 evidence_sha=true`。

**解释边界不能混淆：** 「Stage2 编译了其余八个原生工具的实现源码」≠「这八个工具的自身 ELF 最初不使用 GNU AS/LD/CRT」；本阶段 *构建工具自身* 仍用 GNU PP、GNU AS、GNU 静态链接/libc，避免同时切换太多变量。上述 `return 42` **消费程序**已真正做到 MiniAS+MiniLD 而非 GNU AS/LD，但源程序预处理仍是 GNU GCC。MiniNM、MiniStrip、Driver 本轮已由 Stage2 编译、链接、QEMU 启动，但**未通过独立完整功能门禁**；不能将 5 套完整功能测试虚报为 8/8 套。也尚未声称 Stage2 编译 2064 Linux、用 Stage2-built 整套工具编译 Linux、完全 GNU-free 或工具链第三级固定点。

CI 初次回放 #39043130457 等失败经进一步证实是**Actions 下载 Artifact 将 ELF 执行权限从 `0755` 丢为 `0644`**，并非 Stage2 错编译或 MiniPP 逻辑故障：各工具 SHA 相同，下载后恢复 `+x` 即所有现有功能套件全部通过。端到端第一次还误传 MiniLD 不支持的 GNU `-Ttext`，后改为历史 A2 已验证的 MiniLD `-static -e _start`，无生产源码修改。保留上述失败记录，防止下次复刻环境/CLI 错误。

**下一阶段是自举完整 Linux 路径与剩余工具验证，不是重开无意义性能优化。** 先让当前 Stage2 对真实 Linux GNU-Kbuild `.i` 的汇编和目标对象与 Stage0 匹配，然后用已有七路分片推进 Stage2 2064 + Linux 链接与 QEMU P1；再逐段把 GNU PP、AS、LD 等替换为已经通过单工具及小型联合门禁的 Stage2-built Mini 工具，收敛整条原生 Linux 构建链，而不是一次性宣称所有界面都消除了 GNU 依赖。


## 6. Stage2 构建工具链真正接管完整小型 C 程序的 PP→CC→AS→LD（2026-10-17）

**真实 CI [#39044664602](https://github.com/yituanxing/minic-toolchain/actions/runs/39044664602) SUCCESS**，在前述完整 Stage2-built 8 工具 ELF Artifact、47-TU fixed-point Stage2 编译器 SHA 不变的前提下，保留既有 MiniPP/MiniAS/MiniAR/MiniLD/MiniObjcopy **5/5** 原套件 PASS，且增加第二条真正独立的原生数据流：

```text
C 源 main.c + 本地 answer.h
  (#include; 宏 SUM_TWO(TWENTY_ONE); #if 必须算出 42)
         │
         ▼
QEMU / Stage2-built MiniPP (-E -P -undef -nostdinc) → main.i
         │
         ▼
QEMU / Stage2 minic-cc → main.s
         │
         ▼
QEMU / Stage2-built MiniAS → main.o
         │                 (另有前述 Stage2-built MiniAS 的 _start.o)
         ▼
QEMU / Stage2-built MiniLD (-static -e _start) → ELF64 ET_EXEC
         │
         ▼
qemu-riscv64 ELF：真实退出码 **42** ✅
```

日志原文：`BOOTSTRAP_ALL_MINI_C_CHAIN=PASS Stage2_built_MiniPP=true Stage2_MiniC=true Stage2_built_MiniAS=true Stage2_built_MiniLD=true GNU_PP_AS_LD=false qemu_return=42`，以及 `BOOTSTRAP_STAGE2_NATIVE_RUNTIME=PASS tools=8 suites=5 runtime_chain=42 all_mini_pp_cc_as_ld=true evidence_sha=true`。程序编译、汇编和链接全过程**未调用 GNU 预处理/汇编/链接**，不再只是用 GNU PP 的 `.i` 冒称完整 Mini 流程。程序的预处理输出、汇编、对象和 ELF SHA 连同功能日志保留在 [#39044664602 的 artifact](https://github.com/yituanxing/minic-toolchain/actions/runs/39044664602) 内。

**范围边界：** 构建这些 8 个工具的 **ELF 自身**在 30 TU 阶段仍然使用 GNU GCC -E、GNU AS 和 GNU 静态链接/CRT/libc；所以此次成功是**自举工具运行构成的一条无 GNU PP/AS/LD 的 C 消费程序管线**，**不是**「全部工具源码完成无 GNU 自重建」，也不是 Linux 内核全链 Stage2 运行成功。CLI Driver / MiniNM / MiniStrip / MiniObjcopy 的后续联合测试以及自举 2064 Linux GNU PP→Stage2 CC→GNU AS/LD→QEMU P1，仍是独立验收里程碑。为了避免过度声明，后续测试摘要的 `bootstrapped_no_GNU_preprocessor` 指**完整工具自身构建**，保持 false；新增加 `all_mini_fresh_program_no_GNU_preprocessor=true` 专指上述小型程序。
