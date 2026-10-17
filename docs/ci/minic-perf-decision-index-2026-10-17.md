# MiniC 性能候选决策索引（2026-10-17）

> **新优化实验先查此表，再查 [完整因果实验账本](linux-performance-causal-investigation-v1.md) 和 [回退复核](minic-compilation-regression-triage-2026-10-17.md)。**
> 本表是候选的**状态索引**，不是覆盖旧的原始数据；旧实验报告、补丁、CI artifacts 均保留。每次新优化先记录「相同结构以前试过没有、当时基线是什么、为何现在仍值得做」。
> 基线：主线 `6612e2c1a3`（文档头），最新已认证的 Boolean/Core/Integer 联合源码经 [2064 + GNU link + QEMU P1 #38995964988](https://github.com/yituanxing/minic-toolchain/actions/runs/38995964988) PASS。这个 PASS 是 GNU PP → MiniC → GNU AS/LD，不能声称全 Mini 工具链共同通过。下列不同实验的加速比不能相乘，也不能伪称当前 2064 的同比速度。

## 0. 开卡检查（防止重复劳动）

1. 先按模块、代码位置、**消除的重复工作/成本模型**匹配表中候选；名称不同（例如 `statement_blocks` generation stamp 与 per-TU workspace）不等于新问题。
2. **已集成**：不得将同一个算法重新申请为全新优化；只允许新瓶颈证据下的边际改进。
3. **已拒绝 / 暂停**：不得因“理论减少操作”直接重启旧 A/B。重新打开须有**当前编译器 SHA、同 SHA 冻结 .i、近期 CPU/阶段计时证明该工作仍是热点、以及新机制与旧实验的差别**；先做一轮小样本，不准直接 500/2064。
4. **限定场景候选**：不得将单个热点的结果推广至所有 TU；只在证明该场景出现于当前长尾并有绝对收益时扩样。
5. 每张候选卡固定字段：`candidate_id, status, duplicate_of, baseline_sha, input_scope, stage_cpu_or_wall_evidence, predicted_affected_owners, expected_absolute_saving, stop_rule, CI_run, verdict`。A/B 两臂固定输入和编译器身份，汇编逐字节等价；必要时 ABI / GNU link / QEMU；记录 wall/user/sys、中位数、MAD 及真实单文件回退。无强收益即停。
6. **速度统计需要区分**单 TU 累计 `.i→.s`、真实 Runner 关键路径和 QEMU 验收耗时。缓存复用正确性不等于新版本性能实测。任何失败、负实验及复测都追加回账本。
7. 禁止为“顺带统计”无理由重编 2064：先复用已冻结输入与现有 perf500 job；下一次**本来就需要**的 2064 构建才考虑顺带做全量低开销阶段 census，参见 [census gate](minic-2064-phase-census-plan-2026-10-16.md)。

## 1. 已拒绝或因边际收益不足而停止：默认不重开

| ID / 别名及相同成本模型 | 决策和确切证据 | 重新打开的硬条件 |
| --- | --- | --- |
| **PERF-010** Parser global-name hash（全局符号表哈希） | `not-promoted`；函数名哈希已启用条件下，3 TU P50 合计节省 **0.88%**，filter 退化；[实验 #38655550585](https://github.com/yituanxing/minic-toolchain/actions/runs/38655550585)。旧独立候选已停，不要再反复调哈希。 | 新的真实全局查找占比/探针数明显上升，并非单纯更换哈希实现。 |
| **PERF-011** `statement_blocks` / per-function 整 TU 初始化 → per-TU workspace + touched IDs | `not-promoted`；先前已实现、试验并保留 `tools/ci/apply-perf-core-lower-statement-workspace-v1.py`，三热点 P50 合计 **36.726→36.027s（-1.90%）**；[实验 #38656598585](https://github.com/yituanxing/minic-toolchain/actions/runs/38656598585)。最新代码仍有每函数全 TU 初始化并不意味着此前从未试过。 | 当前版本先分离计时 `malloc`、初始化、真正 map 操作、工作区维护 CPU 成本；如确为新热点，才能考虑不同机制（epoch/generation stamp 或稀疏索引）并明确区别。 |
| **PERF-023** Symbolic Transitive 的额外 CALL-only 索引（在 owner 过滤基础上） | `not-promoted`；三个样本边际约 **5.15%**、集中于 crc32，停止追加同项微调/first500；[实验 #38665476648](https://github.com/yituanxing/minic-toolchain/actions/runs/38665476648)。注意 **Symbolic Owner** 主优化本身已有效，不应一起禁用。 | 当前 Symbolic Transitive 成为显著的未解决成本中心，且优化的是不同操作。 |
| **PERF-033** AST `2x → 1.5x` 动态数组增长倍率 | `rejected`；三热点 CPU/RSS 收益混合，dev 耗时和 RSS 都退化；保持默认 **2×**；[实验 #38676131973](https://github.com/yituanxing/minic-toolchain/actions/runs/38676131973)。这不否定未来改 AST 布局本身。 | 必须提出不同于“只调倍率”的布局/所有权方案，并有真实内存复制/RSS/CPU 归因。 |
| **Core Reentry Lazy Index**（延迟到首次查询才建索引） | `rejected`；500/500 汇编等价，但 **439.569→443.103s，+0.80% 用时**，已撤回，恢复 eager；[实验 #38994826749](https://github.com/yituanxing/minic-toolchain/actions/runs/38994826749)。 | 只允许不同的可计量成本模型，不得重命名为按需建索引后直接重跑。 |

## 2. 已试但限定范围、暂时暂停：不要误标成“从未优化”

| ID / 结构 | 状态与原始证据 | 当前动作 |
| --- | --- | --- |
| **P14 / PERF-012/013** Codegen `OBJECT_ADDRESS` / ValueId usage index | `scoped-promotion-candidate`；filter 约 **1.178×**，其他一组三 TU **47.009→46.006s（-2.13%）**，dev 轻微退化；[#38657320806](https://github.com/yituanxing/minic-toolchain/actions/runs/38657320806)、[#38658389460](https://github.com/yituanxing/minic-toolchain/actions/runs/38658389460)。没有全量/当前版本的通用收益证明。 | 不重复细抠 P14；若最新 Codegen/相似网络输入成为关键瓶颈，才以**新的**冻结慢例复核。 |
| **P15** scalar object interval one-pass（基于 P14） | `paused/low-marginal`；不同早期配置的 5 TU 合计 **1.0339×**，memcontrol 退化；[#37888313913](https://github.com/yituanxing/minic-toolchain/actions/runs/37888313913)。不能说算法永远无效，也不能再次无根据优化。 | 仅在当前 Codegen 独占 CPU / 扫描计数清晰表明此路径仍热门时重开。 |
| **PERF-002 / blake2s CFG slot producer lookup** | 已有具体代码优化、专门历史记录：[blake2s 诊断](linux-blake2s-cfg-slot-lookup-performance-2026-10-13.md)。**不要把过去 33 分钟病理例当成当前瓶颈**。 | 先看当前同 SHA 输入的单文件新计时；不能照抄旧实验。 |
| **Integer direct/initial** clone 路径 | 在 2026-10-16 七 TU 阶段样本中，整数专门化整体已压到 **352.351ms**；Integer owner 与 CALL-only 已解决很多重复扫描。 | 当前阶段排序未证实其热点前，停做进一步 variant/索引微调。 |
| **mini-cc `statement_blocks` epoch/generation stamp** | `untried implementation, same hypothesis as PERF-011`：属于同一个“每函数清整个 TU 映射”问题，不可伪称全新热点。 | 必须先走 PERF-011 重开门槛，然后才比较 epoch 与历史 workspace 的 CPU 开销。 |

## 3. 已集成、已认证：不要把相同优化重新做一遍

- Scoped Boolean Domain Cache（主要旧热点优化）。
- Symbolic FunctionBody Source Owner Index、Integer CALL-only Index、Parser earliest-first Function Hash（三算法联合在 [#38675862094](https://github.com/yituanxing/minic-toolchain/actions/runs/38675862094) 2064/P1 通过）。
- Integer Source Owner Filter（[PERF-035/036，#38927659471](https://github.com/yituanxing/minic-toolchain/actions/runs/38927659471) 2064/P1 通过；500 TU 单因素约 **1.493×**）。
- Core Reentry Incoming Target Index **eager**（[PERF-038，#38992779314](https://github.com/yituanxing/minic-toolchain/actions/runs/38992779314) 2064/P1 通过；500 TU 单因素约 **1.104×**）。
- Boolean Transitive Source Owner Filter（[PERF-039，#38995964988](https://github.com/yituanxing/minic-toolchain/actions/runs/38995964988) 2064/P1 通过；500 TU 单因素约 **1.026×**）。
- 已合并 Parser 三处高频复制优化及既有 typedef、Core/Codegen 先前经过认证的改进。复核源码/版本身份再认定当前构建中开启哪些选项；不要重做旧补丁。
- Performance regression triage 已存在：Core 16 TU 的 8+8；Boolean 12 TU 的 8+8。未发现两种时钟同时明确持续回退的样本，但**未检测的单文件不自动判定无回退**。参见 [回退复核](minic-compilation-regression-triage-2026-10-17.md)。

## 4. 下一轮候选队列：先重新归因，不允许先选算法

**当前下一张卡是诊断，不是新优化实现。** 上次七 TU 阶段归因是 **Core eager + 最新 Boolean Source Owner 之前**的源码版本；无法无条件复用当时的 `core-set≈4.926s / 7 TU` 做当下排序。[原审计 #38930177529](https://github.com/yituanxing/minic-toolchain/actions/runs/38930177529)。

1. **最先做**：对最新经过 2064/P1 认证的 MiniC 身份，复用原 first500 冻结缓存和旧 `[minic-next-stage-audit]` 的**七 TU**分段诊断（已有脚本和已有 perf500 job，不增加 workflow/job）。以现版 CPU/wall 阶段与热点排序为输出；额外插桩输出要核对未插桩基准的 `.s` SHA；不把一次阶段日志当成性能改进的 A/B。
2. 若 `core-set` 仍第一，先在内部定位具体 Lowering CPU 消耗，**排除**重复 reentry 索引和 PERF-011 的旧假设；若此前测过的结构仍是大头，必须先写“重开理由”。
3. 若 Parser / Symbolic / Codegen 成为第一，查现有已集成优化和 P14/P15/Parser-global/Symbolic-CALL 的状态再确定新结构；候选应解释为何旧优化不覆盖这项工作。
4. 更大范围：2064/TU 的 **低开销全量分阶段 census 尚未完成**，随下一次必要的真实七路全量构建顺带采集；不要只为它重编全量。最终要能按全体累计耗时、P95、单文件最大值和最慢 Runner 的关键路径分别排序。
5. 另有未归因的 `.o` 体积问题：历史 2064 TU 中 MiniC/GNU 大小约 **456.5MB/66.4MB（6.88×）**，只是一条应复用旧对象审查 ELF sections/relocations 的线索，**不等于已证明编译器 CPU 热点**；参见 [全量记录](linux-performance-causal-investigation-v1.md)。

**本轮终止条件：** 无新当前版本阶段/CPU 数据 → 不改算法；出现已拒绝候选 → 对照本表核对重开门槛；只有“当前热点 + 旧方案未覆盖的结构 + 预计绝对秒数 + 正确性路径”均成立才开实现卡。不得让局部微优化阻碍全局轮换。


## 5. 2026-10-17 最新主线重新归因（不是新 A/B 优化）

**当前源码身份**：`454378bc1`（只含文档/工作流注释差异，生产 C 源与已认证 `6612e2c1` 相同）。已有 perf500 Job 复用 first500 SHA-locked 缓存，实际只对其中七份真实 Linux .i 做分段诊断。运行 [#38997438501](https://github.com/yituanxing/minic-toolchain/actions/runs/38997438501) **SUCCESS**；7/7 `.s` 与计数插桩后的输出 SHA 一致；无新算法修改，无 2064 重编，无 QEMU 运行。

| 分段（七个选择性样本合计，当前诊断编译器） | 实测 ms | 本轮含义 |
| --- | ---: | --- |
| Parser | **1,255.239** | 本组选定样本最高的已明确阶段；原函数哈希、Parser copy 等已优化，**先定位新 CPU 热点** |
| Core Function Set | **1,058.396** | 低于 Parser；已有 eager Reentry 生效，不能从过去 4,925.616ms 在不同测量运行中的读数直接断言某项严格加速比 |
| Symbolic Specialization | 732.996 | Owner 已集成，需先判断剩余调用热点再开新卡 |
| Codegen | 278.164 | P14/P15 当前没有明显的全局优先权 |
| Integer Specialization | 268.685 | Integer CALL/Owner 已集成，暂不复做 |
| Boolean Specialization | 194.998 | 最新 Transitive Owner 已集成，暂不复做 |
| Core Validate（独立阶段） | 33.851 | 更不应优先细抠 |

同一 run 的 `map_calls=5,854` / `map_slots=294,098,527` 仍显示每函数全 TU `statement_blocks` 初始化真实存在，**但这已经是 PERF-011 试过的算法假设**。此前数字约 1.18GB 是累计逻辑写入，不能当作 DRAM traffic 或节省时间。现在即使所有 Core-set 工作都被消除（显然不可能），七样本上界也只是这个阶段的 1.058s；这不足以让旧 Workspace 自动跃居第一。

**决定**：不要实现 Workspace v2、Generation Stamp 或继续 Boolean/Core Reentry 的微调。下一步用已存在、会追踪真正 `bin/minic-cc` 的 `[minic-cpu-profile-only]` 对两个冻结输入做 CPU 函数归因，尤其确认 Parser 占用是不是未优化的新问题。该 CPU 指令剖析不等于 wall time，也不能自动推广至 2064 TU。基于它再开**新的不同成本模型**候选，而不是预设增设索引/哈希。已触发同一 perf500 Job 的 run [#38997549409](https://github.com/yituanxing/minic-toolchain/actions/runs/38997549409)；本行仅记录开始，**最终状态须按对应日志判定**。
