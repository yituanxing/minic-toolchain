## 最新修正：2064 文件**成对实测数据已经存在，且已复原**（2026-10-15）

- 真实运行：[GitHub Actions #38679636723](https://github.com/yituanxing/minic-toolchain/actions/runs/38679636723)，commit `67eef0e741164ca0a1307cd407fdb2baaa033559`。
- 实际七个 Kbuild shard **全部 `COHORT_COMPILE=PASS`**，旧版 Runtime/Crossblock MiniC 与增加 FunctionBody Owner、整数 CALL 索引、Parser 函数哈希的优化版运行在相同 GNU .i 上，每个目标的两份 .s 字节相同。
- 后处理失败原因：原统计器错误要求原始编译调用数等于 2064；Kbuild 合法地编译额外的辅助对象，并重编译少量 .o，实际记录 **2087** 条。
- 已从七份原始 `perf-ab.tsv` 和相同七路已通过正确性验收 #38675862094 的各份 `full-objects.txt` **严格匹配**，每片完整覆盖所需 295/294 文件，对重试目标取最后一条记录，剔除不在目标名册的辅助文件。**最终得到 2064 个唯一对象完整配对**。原始七片数据全部来自已经运行过的 CI，没有重编数字。
- **优化前累计 .i→.s：18,606.071 秒**
- **三项联合优化后累计 .i→.s：14,261.185 秒**
- **全量 A/B 加速：1.3047×；耗时减少 23.35%；累计节省 4,344.886 秒。**
- 七片分别为：0: 1.278×；1: 1.370×；2: 1.302×；3: 1.287×；4: 1.284×；5: 1.288×；6: 1.312×。单文件有 331 个时间回退，不能遗漏这一事实。
- 新版最慢 C 目标：`net/core/dev.o` 91.451s、`net/core/filter.o` 83.040s、`net/ipv6/ip6_output.o` 69.419s、`net/core/skbuff.o` 61.063s、`net/core/sock.o` 60.522s。
- 重要：这里是 **2064 个 .i→.s 单文件 wall time 的累计**，不包含 GNU -E、汇编或 Kbuild 七路实际墙钟；执行环境有并发竞争。实际同时跑 old+new 的七片 Kbuild 最慢约 936.522s（两版都被编译，**不能**作为任一单版耗时）。
- 此前错误说“新三项联合没有全量时间”，实际**测量存在，坏的是后处理统计器，并且没有及时读失败 CI 及其保留的原始 artifact**。已修正现存 `tools/ci/linux-perf-2064-paired-compile-v1.py`，不得重新跑 2064 文件来弥补这种可直接从缓存 artifact 恢复的统计失败。

# MiniC 2064 个真实 Linux C 文件：计时证据与无法比较项（2026-10-15）

## 事实：过去**有**全量计时，我之前的“没有时间”说法不准确

原始七路冷编译证据：[GitHub Actions #38204602644](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644)，checkout commit `d5dde03594665842f2bb3b8556f842f511a5dfa4`，2026-10-12，全部七个 `gcc-shards` job 为 SUCCESS、全部有 `COHORT_CANDIDATE_CACHE=MISS`、`COHORT_COMPILE=PASS`。这些是真实的旧版 Linux Runtime-profile Kbuild 编译耗时，**不是最新三项联合索引优化的对照测试**。

| shard | owner .o | MiniC 执行次数（含 Kbuild 额外重建） | MiniC 累计时间 ms | Kbuild cohort 实际墙钟 ms | 证据 job |
| ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 295 | 299 | 3,080,591 | 563,567 | [114665184838](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644/job/114665184838) |
| 1 | 295 | 299 | 2,968,841 | 545,887 | [114665184600](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644/job/114665184600) |
| 2 | 295 | 297 | 2,807,031 | 517,947 | [114665184794](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644/job/114665184794) |
| 3 | 295 | 297 | 2,771,549 | 515,090 | [114665184845](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644/job/114665184845) |
| 4 | 295 | 298 | 2,608,551 | 473,997 | [114665184766](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644/job/114665184766) |
| 5 | 295 | 299 | 2,498,417 | 452,792 | [114665184804](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644/job/114665184804) |
| 6 | 294 | 298 | 2,549,429 | 466,256 | [114665184817](https://github.com/yituanxing/minic-toolchain/actions/runs/38204602644/job/114665184817) |
| **总计** | **2064** | **2087** | **19,284,409** | **最大 shard 563,567** | |

说明：
1. 每片 `COHORT_FULL_COMPILE elapsed_ms` 是**包含 GNU -E、MiniC -S、GNU AS、Kbuild 重试和证据复制**的墙钟；七路并行的 cohort 部分在同一轮的尾部约以最慢片 **563.567 秒（9 分 23.567 秒）**为界，不等于整个 CI（包含准备/下载/链接）的墙钟时间。
2. `COHORT_STAGE_TIME stage=minic count=...` 相加是 **2087 次调用、19,284.409 秒**，**不是严格 2064 次**，多出的 23 次来自 Kbuild 额外调用/重试，不能谎称“2064 文件准确累计耗时”；这是之前旧版的近似背景量级，不是最新候选 A/B。
3. 新三项联合已在 [#38675862094](https://github.com/yituanxing/minic-toolchain/actions/runs/38675862094) 以七路缓存对象完成 Linux/QEMU 正确性验证；但该次日志为 `COHORT_CANDIDATE_CACHE=HIT` / `COHORT_COMPILE=SKIPPED`，**没有**同输入新旧版本的 2064 文件性能数字。**不能**用 #38204602644 与缓存命中 CI 工作流持续时间计算优化加速。
4. 10 月 14 日另有真正成对的 first500 A/B [#38571430622](https://github.com/yituanxing/minic-toolchain/actions/runs/38571430622)，1638.145→595.860 秒（2.7492×），但**优化组合、样本范围不同**。函数名 hash 增量 [#38669659296](https://github.com/yituanxing/minic-toolchain/actions/runs/38669659296) 在 BTF/verifier/filter 三文件分别 1.4398× / 1.3553× / 1.2036×，这也不等于全量加速。

## 本轮优化实际上怎样分析

- 先用现存 `tools/ci/linux-perf-phase-trace-v1.py` 的真实 .i 阶段计时找到 `normalize→core` 高占比间隔（含 integer/symbolic specialization），而非凭空猜 CPU 热点。
- 再读 `src/frontend/function_body.c`、`src/frontend/parser_core.c` 和 runtime inline specialization 补丁，识别每次函数变体克隆后多次全 TU 扫描、CALL 扫描和函数声明线性查找；按保持语义的 owner 索引 / CALL 索引 / 函数查找 hash 独立优化。
- 依靠同 SHA 固定 .i 的 paired A/B 比较单项增量耗时，检查汇编 bytes 一致，并扩展为 500/500 汇编等价和七路 Linux/QEMU 正确性。
- 缺失的一环：**三项联合**对**2064**文件，在相同 runner、相同 GNU 生成 .i 下旧/新工具链的**真实全量计时**没有与之前的正确性验收绑定；缓存命中把这一层跳掉。代码分析能证明候选高频/复杂度，局部 A/B 能证明局部收益，**两者都不能替代全量加速结论**。

## 全量性能交付硬条件（所有未来性能优化均应遵守）

1. 先明确版本 SHA、输入 SHA、测量边界是 `.i→.s` 还是 `C→.o`、计时口径、原版和候选优化列表。
2. 实际执行冷测（或对 SHA 固定 .i 的新旧工具链双臂）并留存 **old_total_ms、new_total_ms、speedup、regression_count**；禁止 `COHORT_COMPILE=SKIPPED` 的记录通过性能门。
3. 七个 producer 分片每片报告 `COHORT_COMPILE=PASS` 和 A/B 清单，必须恰好覆盖 2064 个唯一对象；失败/超时的分片不可“加总剩余六份”得到完整结果。
4. 汇总和真实 runner 墙钟分开；单 TU 累计计时含系统竞争噪声，优先相同输入、交错顺序、重复测量，报告区别。
5. 保留 correctness oracle（汇编 SHA、GNU as、ABI、必要时 GNU 链接+QEMU）；只达成正确性不报告性能加速倍数。
6. 每次优化先记录：为何判断是瓶颈、旧版时间、修改点、局部新时间、全量新时间（如尚无要明确 **PENDING**），以及收益不佳的终止条件。

**当前全量优化结论状态**：更新见文首：已从 #38679636723 的七片 raw artifacts 恢复同条件 2064 文件 paired A/B，**1.3047×、降低 23.35%**。旧全量时间继续作为独立历史参照。
