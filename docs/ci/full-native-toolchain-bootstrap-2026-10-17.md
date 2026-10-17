# 现役 9 工具原生全链自举：Stage2 → Stage3 → Stage4

状态：**尚未取得全链 PASS；实际执行和失败记录保留，不得把之前的 GCC 辅助 Stage2 固定点冒称原生全链自举。**

## 输入与历史事实

- 现役编译器生产源码：`Makefile:MINIC_SOURCES` 与 `tests/compiler/bootstrap/b0-sources.txt` 一致，47/47。
- 自举固定点：`#39040013459`，可信 Stage2 RV64 `minic-cc` SHA256:
  `17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d`。
- 使用 Stage2 编译另外 8 个原生工具：`#39042731803`，实际 30/30 TU；此 ARTIFACT 自身最初经 GNU PP/AS/LD 链接，仍只是可信种子。
- Stage2-built 8 套原生工具门禁及由 Driver 调用 PP/CC/AS/LD 构建小程序 QEMU return42：`#39045009260`，SUCCESS。
- Stage2 Linux 500/500 各个汇编和对象身份一致：`#39044397806`，SUCCESS；另外的 Stage2 Linux 2064 + QEMU `#39049610848` **已单独成功：7/7 分片、2064/2064 Stage2 C 对象、GNU 最终链接、QEMU P1=PASS（12 syscalls）**。这只证明 Stage2 MiniC 全量 Linux 运行时正确性，不能混同于九工具原生自举。

## 真正的全链路验收

执行脚本 `tools/ci/minic-bootstrap-full-native-v1.py`，复用现有
`.github/workflows/linux-runtime-optin-perf-suite-v1.yml` 的唯一 `perf500` Job。
无需新的 workflow/job，不修改生产 C 代码。

```
可信 Stage2 RV64 MiniC + SHA 认证的 Stage2-built MiniPP/AS/AR/LD/NM/Strip/Objcopy/Driver
  └── 对 Makefile 当前全部 77 个不重复 C TU：
      Stage2-built MiniPP -> Stage2 MiniC -> Stage2-built MiniAS -> Stage2-built MiniLD
          └── 构建 9 个 Stage3 RV64 ELF（含 minic-cc）
              └── Stage3 9 个工具再次对同样 77 个 C TU 原生预处理/编译/汇编/链接
                  └── 构建 Stage4 9 个 ELF
                      └── 比对 77 个 .i/.s/.o SHA
                      └── Stage4-built MiniObjcopy 提取两个阶段的可加载映像并逐一比较
                      └── QEMU 实际运行 9 个工具
```

**失败关闭规则**：脚本自身仅执行 `qemu-riscv64` 对原生工具的调用；
Runner 通过 `strace -f -e execve` 对完整脚本记录执行族，禁止任何 GCC/GNU AS/GNU LD/GNU objcopy 回退。记录 `full-native/execve.log`、`full-native/verdict.log`、逐 TU 源和产物 SHA、stderr 和首失败。

注意：**Stage3/4 目前仍复用外部预构建的 GNU 目标头文件、glibc、CRT 与 libgcc 静态归档作为二进制种子。** 这意味着若门禁通过，只能宣称“Stage3/4 自身构建没有调用 GCC、GNU AS/LD”，并不能宣称“从源码独立构建了 libc/CRT/libgcc”或“所有输入不含 GNU 来源”。真实的完全脱离 GCC 需要后续用 Mini 全链重建这一层运行库（可考虑仓库现有 musl 静态集成用例），并用自己的 freestanding 标准头替代 GCC 内建头。

## 实验过程与禁止回退

| CI | 结果与结论 |
| --- | --- |
| #39053315960 | 首次 77 TU 原生尝试：MiniPP 嵌套 `#include "/absolute/path"` 未解析。定位为脚本入口绝对路径处理；改为在 overlay 内直接内联目标预定义文本。 |
| #39053483280 | 77/77 缺 `stddef.h` 或 `stdbool.h` 等 GCC 内建目标头文件；将其定位为外部只读头文件种子，显式记入依赖清单，禁止启动 GCC。 |
| #39053593540 | MiniPP 完成预处理，31/77 编译/汇编 PASS；46/77 在 Stage2 语义分析阶段被 `use of undeclared local` 拦截。比较同源码的 GNU C11 固定输入，发现误用了 Linux `-std=gnu11 -fshort-wchar -funsigned-char` 目标预定义，使 glibc 打开额外 GNU 扩展（例如 `__bswap_16` 中内建），且 `wchar_t` 类型不同。不能把它归咎于 46 个独立编译器 bug。 |
| #39053974234 | 已按 C11 hosted ABI overlay 改为 `__STRICT_ANSI__`、原生 `wchar_t`、signed char 和 lp64d，真实源码四个独立样本 **4/4 MiniPP→MiniC→MiniAS PASS**（core_ir、preprocessor、minic-cpp、MiniLD linker）；strace **GNU 编译/汇编/链接进程为 0**。 |
| #39054222221 | 全部 77 TU 源码真实进入 MiniPP/MiniC/MiniAS；其中 **76/77 全程 PASS**。唯一失败 `src/core/core_lower.c`：Stage2 MiniC 已成功生成 **14,848,436 字节、888,244 行汇编**，Stage2-built MiniAS 在原有 **240 秒**超时。其余 76 个 ELF .o 均有逐文件 SHA 记录，脚本 strace 查明无 GNU PP/AS/LD 执行。尚未开始九工具最终链接。 |
| #39055271159 | **Stage3 77/77 原生 MiniPP→Stage2 MiniC→Stage2-built MiniAS 已全部成功！** 76 个 .o 经 SHA 缓存复用，唯一大文件 `src/core/core_lower.c` 原生 MiniAS 在 187.237 秒完成。Stage3 链接随后阻塞于 `minic-ld: missing-symtab:/usr/riscv64-linux-gnu/lib/crti.o`，与 MiniC/MiniAS 无关。未回退 GNU LD。 |
| #39056854507 | 从已证实的 77/77 原生 .o 直接进行 MiniLD 链接探测，不重复编译：移除 glibc `crti.o/crtn.o` 后揭示下一处真正阻塞 `unsupported-section-type:iofclose.o:index=1:type=17`（glibc `libc.a` 的 `SHT_GROUP`）。这是 MiniLD 当前已知的实现范围问题，不能视作编译器失败。 |
| #39057022666 | 用仓库现有的 MiniLD+musl 静态链接经验，单独生成经过 tarball SHA 验证的 **musl 1.2.5 外部二进制运行库种子**。MiniLD 用同一组 77 个 Mini 原生 .o 中的 MiniPP 对象链接 **成功**，且生成的 RV64 MiniPP 经 QEMU `--help` 运行成功。链接测试 `strace` 证实 GNU GCC/AS/LD 调用为 **0**。musl 最初由 GNU 编译器生成，明确记录其种子来源，**不称作 libc 原生自举**。 |
| #39057243168 | 恢复上一轮固定 SHA 的完整 77 个 .o 和 musl 运行库种子 Artifact，本次无需重建 musl，也不在严格自举执行链调用 GCC。继续真正的 **Stage3 全九工具原生链接、Stage4 再构建与可加载镜像固定点**；在结果返回前不得宣布九工具全链通过。 |

每次实验按 `stderr→源 .i→标准/ABI 前提→最小有界回放→全量` 调试，不能重复修改已经证实正确的 47 个编译器生产 C 源。

## 接下来严格验收

- 全 77 个 TU 的原生源输入与成功数必须 77/77。
- Stage3/4 各 9 个原生工具的链接成功、执行成功，以及每个源的相同条件输入与对象输出身份。
- 扩大到 9 个工具的真实差分测试、Driver 实际 C → ELF → QEMU，以及和历史原生验证相同的功能门禁。
- 单独列出保留的系统库/CRT 依赖；若 libgcc/glibc 不能被 MiniLD 直接消费，复用仓库已存在的 musl 相关工程测试，而不是暗中降级回 GNU LD。
- 只有复用自身新生成的完整工具链、并在严格进程执行审计下通过全部固定点，才能标记 Stage3/4 native fixed point PASS。原生 CRT/libc 重建是再下一层独立门禁。

### 常用链接（准确记录）

- 最新全九工具 musl 静态全链自举：`https://github.com/yituanxing/minic-toolchain/actions/runs/39057243168`
- 已验证 musl MiniLD 原生链接/运行：`https://github.com/yituanxing/minic-toolchain/actions/runs/39057022666`
- Stage3 77/77 原生 C→目标文件：`https://github.com/yituanxing/minic-toolchain/actions/runs/39055271159`
- 首次 77 TU 76 成功的完整日志：`https://github.com/yituanxing/minic-toolchain/actions/runs/39054222221`
- Stage2 Linux 2064 + QEMU P1: `https://github.com/yituanxing/minic-toolchain/actions/runs/39049610848`
- 4 TU 已验证前导：`https://github.com/yituanxing/minic-toolchain/actions/runs/39053974234`
- 最初二进制与 Stage2 固定点：`https://github.com/yituanxing/minic-toolchain/actions/runs/39040013459`
