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

## 新的 CPU 归因与历史差异复查（2026-10-17）

- [CPU Profiler #38997549409](https://github.com/yituanxing/minic-toolchain/actions/runs/38997549409) **SUCCESS**，真实 `minic-cc` Callgrind，自我指令数 Ir：`fs/ext4/fsync.o` 中 libc memcpy 44.53%、Symbolic specialization 7.10%；`kernel/bpf/btf.o` 中 memcpy 28.43%、Symbolic 9.99%、`minic_parser_bind_enum_constant` **6.42%**、`minic_parser_span_equals` 4.11%。这是指令占比，**不是独立 wall-time**。输入 .s 与 release 逐字节相同，工具 SHA 已固定。
- 原来已优化 `parser_type.c` 的三个 Parser 复制点，不等于所有位置。余下 top-level enum/record probe 的名义拷贝约 133,664B/次，BTF 约 3707 次；[2-TU 单因素 #38998023186](https://github.com/yituanxing/minic-toolchain/actions/runs/38998023186) **SUCCESS**，保持当前生产 C 不变，仅 CI 临时应用 `tools/ci/apply-perf-parser-top-level-lexer-lookahead-v1.py`。fsync **476.937→473.769ms（1.0067×）**、BTF **1356.999→1332.823ms（1.0181×）**，两者 P50 合计 **1833.936→1806.592ms（1.0151×）**，4 次/臂交错及 2/2 汇编字节一致。**决策：not-promoted / stop**；这两处局部只是约 1.49% 减时，禁止为其跑 500/2064，也不要把 133KiB×次数直接当作真实 DRAM 流量。保留历史修复脚本和验证产物作为负例。
- 下一条**不同成本模型**的待验证猜想：`minic_parser_bind_enum_constant` 在顶层全局作用域仍对每个新增枚举常量以逆序线性检查是否重名；全局查找 `minic_parser_find_enum_constant` 的有序 hash **早已存在**，所以只测试是否可在 `scope_count == 0` 的 bind 检查中**复用该索引**，而不是再造一个 enum hash。非全局作用域保留现有线性语义；同步/OOM 使用已有线性 fallback。此项尚无真实 A/B 倍率，需单因素、相同文件/工具配对及 .s 严格相同。
 
## 最新单因素筛选：复用已有 Enum Hash（2026-10-17）

[CI #38998220260](https://github.com/yituanxing/minic-toolchain/actions/runs/38998220260) **SUCCESS**，测试代码仅在 CI 临时 checkout 应用 `tools/ci/apply-perf-parser-enum-binding-global-index-v1.py`；未修改生产 `parser_enum.c`。重用正式源码已有的 `minic_parser_find_enum_constant` 全局索引；只改变 `scope_count == 0` 的 enum binding 重名检测，原局部作用域线性检查、查找失败 fallback、AST 语义保留。对两个 GNU-Kbuild SHA 固定真实 Linux .i，old/new 每臂 4 次交错、2/2 `.s` 严格字节相同：

| owner | 原版 P50 | 重用 Hash P50 | 提速 |
| --- | ---: | ---: | ---: |
| fs/ext4/fsync.o | 483.577ms | 471.457ms | 1.0257× |
| kernel/bpf/btf.o | 1334.350ms | 1245.474ms | **1.0714×** |
| 两文件合计 | 1817.927ms | 1716.931ms | **1.0588×（累计减时5.56%）** |

**独立五文件进一步验证完成**：[CI #38998362636](https://github.com/yituanxing/minic-toolchain/actions/runs/38998362636) **SUCCESS**。新一组 `kernel/events/core.o`、`mm/memcontrol.o`、`fs/ext4/super.o`、`mm/hugetlb.o`、`mm/vmscan.o` 均从既有 SHA 锁定 first500 取真实预处理输入，old/new 4 次/臂交错配对，**5/5 汇编字节一致**，未复测已认证的 fsync/BTF。P50 逐项：1561.077→1507.162ms（1.0358×）、1401.709→1394.276ms（1.0053×）、906.092→893.320ms（1.0143×）、914.388→921.689ms（0.9921×）、779.634→765.774ms（1.0181×）。五文件 P50 之和 **5562.900→5482.221ms（1.0147×；仅约 1.45% 耗时下降）**。一处轻微回退尚未多轮独立确认，不能作为确定的算法退化；但广泛收益远小于初筛中的 BTF。

**最终决策：`verified-local-but-not-promoted / stop`。** 两批七样本算术合计 `7380.827→7199.152ms`，约 **2.46% 耗时降低**；因两个 CI 的环境不同，只作范围估计而非单一严格 7-TU 同 Runner A/B。单样本最佳 7.14% 仍低于旧晋级强收益筛选线，扩样仅约 1.45%。保持生产 enum binding 原实现，不为了微调该 Hash 再跑 500/2064。保留 opt-in 补丁和所有正负结果。下一张卡重新按当前 CPU/阶段占用排序，查与历史方案是否重叠。

**历史初筛判断（已被五文件扩样取代）：** 该方案与此前已集成的 Enum 哈希查找不是重复实现：仅将其应用于此前仍 O(N) 的新增枚举常量重名检查。它不是阈值明显满足的 >10% 大热点收益，故不直接跑完整 500/2064。先在另一组独立 Linux 真实对象评估边际成本并确认收益分布；若依然不足，记录为 `not-promoted` 并转移目标。此次 QEMU/全量未执行，不能声称原生生产源码已优化 5.88%。

## 新候选：AST Verifier 不完整数组语义所有者 O(A×E) 扫描（2026-10-17）

**与旧优化不重叠**：`src/frontend/ast_verifier.c` 的 `incomplete_array_has_semantic_owner`，每遇一个不完整数组类型，都按完整 `Program.expression_count` 从头寻找是否有引用者，找不到还依次扫描别名、全局、局部、Record Field、函数签名等。**两个 verify 阶段 PARSED 与 NORMALIZED 都执行这项正确性检查**。这是校验算法成本，不是此前被拒绝的 Parser 大结构复制、Parser Hash、Core Workspace/Reachability Index 或 Symbolic CALL-only 索引。

现有 **当前生产版原始 Callgrind** [#38997549409](https://github.com/yituanxing/minic-toolchain/actions/runs/38997549409)：`kernel/bpf/btf.o` 的 `incomplete_array_has_semantic_owner` 表达式主循环自身约 **159,012,258 Ir（1.55% 总指令）**；`fs/ext4/fsync.o` 对应 **72,402,178 Ir（1.93%）**。另外 `type_owns_array_descriptor` 条件判断 BTF **80,368,488 Ir（0.79%）**、fsync **36,794,920 Ir（0.98%）**。这些指令已经在两阶段实际执行，不可以和 caller inclusive 重复累加，也不直接等价于 wall-time 百分比。

**不同复杂度候选**：用仅在**单次 AST verifier invocation 内有效**的 bitmap，在进入 ARRAY_TYPE 验证前从现存 expressions/type aliases/global/locals/records/functions 一次性标记拥有者；`array_type_id` 的查询 O(1)，不再为每个数组类型扫描所有表达式。严格遵守 expression/typedef 非 pointer 引用与其它类别只认 pointer 的原规则，以及 extern global array 的特殊例外。bitmap OOM 时保留原线性回退；正常和失败路径释放；不删除 PARSED 或 NORMALIZED 的独立完整校验。候选只用 `tools/ci/apply-perf-ast-array-owner-pass-v1.py` 在临时 CI checkout 修改 `ast_verifier.c`，正式源码仍保持 2064/P1 已认证版本。

**五文件局部 A/B 已完成**：[CI #38999873072](https://github.com/yituanxing/minic-toolchain/actions/runs/38999873072) **SUCCESS**；全部 GNU Kbuild 输入 SHA 固定，old/new 各四次交错，**5/5 汇编完全一致**：btf **1134.128→1094.960ms（1.0358×）**、fsync **402.568→384.805ms（1.0462×）**、events-core **1499.724→1464.439ms（1.0241×）**、memcontrol **1340.017→1297.097ms（1.0331×）**、ext4-super **905.766→871.492ms（1.0393×）**，合计 **5282.203→5112.793ms（1.0331×；减时 3.21%）**。这是真实但**经过热点选择**的局部增益，不能泛化为 Linux 500/2064 平均增益。

**500 文件硬验证成功，值得晋级完整 Runtime 测试。** [CI #38999997399](https://github.com/yituanxing/minic-toolchain/actions/runs/38999997399) **SUCCESS**，500/500 frozen Linux .i 编译通过且 **500/500 `.s` SHA 完全相同**，复用现有 4-worker balanced-order A/B；old **379184.989ms**，candidate **363844.060ms**，单次累计编译耗时节约 **15.340929 秒**，加速 **1.04216×（耗时约减少 4.05%）**。250/250 轮换先跑顺序，单文件倍率中位数 **1.04199×**。500 单轮有 **52 文件**候选 wall 较慢（与之前 Core Name Hash 的 206/500 对比）；最大的 `fs/nfs/client.o` 894.984→1020.799ms、`kernel/events/core.o` 2896.412→2942.118ms、`kernel/cgroup/legacy_freezer.o` 630.951→672.051ms，必须保留回归名单，后续通过重复 wall + CPU clocks 复核，不按单次噪声推断算法必然回退。

**最终生产资格：`runtime-certified / integrated`**。已把上述完全相同的索引逻辑落实到正式 `src/frontend/ast_verifier.c`（[生产代码提交 f2bc0fcd](https://github.com/yituanxing/minic-toolchain/commit/f2bc0fcd6bd9d0c83bb1fa0dbe71d7fe5ecc127d)，新 blob `24c639a7...`）。[Seven-shard Linux/QEMU CI #39000339623](https://github.com/yituanxing/minic-toolchain/actions/runs/39000339623) **SUCCESS**：7/7 实际 shard SUCCESS，`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`，`COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`（证明复用了当前 run 汇聚的 2064 个对象，而非重新运行昂贵前端），`COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`；仍然是 **GNU 预处理 → MiniC 编译 → GNU 汇编/链接 → Linux QEMU P1** 的单工具替换边界，不能写成全 Mini 原生工具。 [Focused T1 #39000287494](https://github.com/yituanxing/minic-toolchain/actions/runs/39000287494) **SUCCESS**：8 项独立测试共用一次编译。**现阶段有冻结 first500 4.216% 总体加速、500/500 .s 一致、正式 2064/QEMU 运行时资格；尚没有严格全 2064 的 old/new 全部 Wall-Time 配对速度数字。**

首个 500 同机单轮慢于旧版的 **52 文件**不是 52 个已经确认的回归。Top8 原始 outlier + 3 个收益控制的 8+8 wall/user/sys CPU+MAD 复核已在既有 perf500 job [#39000717601](https://github.com/yituanxing/minic-toolchain/actions/runs/39000717601) 启动，最终分类需依日志填报，不得单轮定性。

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

## Parser string Lexer-only lookahead：不同于已拒绝的顶层 Probe（2026-10-17）

[原始 CPU census #38997549409](https://github.com/yituanxing/minic-toolchain/actions/runs/38997549409)：当前正式 MiniC 的 Parser sizeof=133,664 bytes；`parser_string.c:689` `parse_string_text` 的 Parser 复制 fsync **5128** 次、BTF **6257** 次，逻辑累计分别约 685MB/836MB（**不是实测 DRAM 流量**）。已拒绝的 `parser_function.c` 顶层 enum/record probe（[#38998023186](https://github.com/yituanxing/minic-toolchain/actions/runs/38998023186)）命中量、实现位置均不同，不能作为这个热调用点的否决证据。

仅在 CI disposable checkout 应用 `tools/ci/apply-perf-parser-string-lexer-lookahead-v1.py`；把 `minic_parser_parse_string_text` 的**完整 Parser 副本**换成只保存 `MinicLexer + MinicToken`，保留源文本解析及原 Parser 上的诊断，生产源码不变。第一轮 [CI #38999333053](https://github.com/yituanxing/minic-toolchain/actions/runs/38999333053) **SUCCESS**，原 GNU-Kbuild SHA-frozen 两文件各臂 4 次配对，`.s` 2/2 逐字节一致：fsync **479.884→454.228ms（1.0565×）**、BTF **1407.220→1376.280ms（1.0225×）**，合计 P50 **1887.104→1830.508ms（1.0309×）**。不能称全 Linux 快 3%。

**扩组结论：`verified-local-seven / not-promoted / stop`**。第二批 [CI #38999421635](https://github.com/yituanxing/minic-toolchain/actions/runs/38999421635) **SUCCESS**：events-core/memcontrol/ext4-super/hugetlb/vmscan 各自 4/4 交错测量、**5/5 相同汇编**，分别约 **1.0049×、1.0136×、1.0116×、1.0207×、1.0264×**，合计 **6392.999→6305.993ms（1.0138×）**。两批七个不重复的 owner P50 之和约 **8280.103→8136.501ms（1.0176×）**；因为来自两轮独立 CI，只能作为不同样本的算术合计而非一次严格七 TU A/B。五文件扩样没有强收益，符合先前“低于显著阈值即轮换”的决策，因此不合入主线、不跑完整 500/2064、不继续逐一针对原 `parser_string.c` 复制做微调。保留独立脚本和正反证据。

## 接下来值得验证的不同瓶颈（先调查，不抢先实现）

已复核 `src/compiler/compiler.c`，存在 `minic_core_named_function_id()`：Core Reachability 遍历 `CALL` / `FUNCTION_ADDRESS` 时，针对每个目标名重新按 `program->function_count` 线性扫描所有源函数，比较 assembler symbol/length 并取**第一**个匹配。当前仓库未发现这条 Core 名称解析路径已有同等索引；它不同于已集成的 **Parser 函数名哈希**（Parser 作用域/名称表）和 **Core Reentry Incoming 索引**（goto/asm-goto 源流向）。来自真正 `minic-cc` 的 [#38997549409](https://github.com/yituanxing/minic-toolchain/actions/runs/38997549409) BTF Callgrind：`minic_enqueue_core_named_function` 自身约 **281.7M retired-simulated instructions / 2.75% Ir**，但这不是准确 wall-time 或已证明线性解析占比。

**已可直接从原始 Callgrind 调用树复用的计数（不重新跑 profiler）：** BTF 的 `minic_enqueue_core_named_function` **1,643 calls**，inclusive **430,399,433 Ir（4.21%）**，self **281,724,220 Ir（2.75%）**；fsync 仅 **36 calls**，inclusive **6,437,463 Ir（0.17%）**。这个大小对照支持“多次名称解析在大型 TU 上构成真实成本”的假设，但不能直接判定循环平均扫描数（编译器会内联 helper）或者 wall time。BTF 作为候选热点、fsync 作为自然小样本对照，比重复优化已被淘汰的 Parser Global Hash 更有针对性。

**真实局部 A/B 初筛（2026-10-17）**：[CI #38998913682](https://github.com/yituanxing/minic-toolchain/actions/runs/38998913682) **SUCCESS**。当前已验证生产版本为 baseline，候选只在 CI 临时 checkout 执行 `tools/ci/apply-perf-core-reachability-name-index-v1.py`；索引构建一次，保证相同 assembler symbol 取最早函数 ID，分配失败回退原线性搜索，作用域仅 Core reachability final pruning。5 个 frozen first500 Linux `.i` old/new 各四次交错配对，**5/5 输出汇编严格字节相同**。

| Linux owner | old P50 ms | new P50 ms | speedup |
| --- | ---: | ---: | ---: |
| kernel/bpf/btf.o | 1405.635 | 1318.361 | **1.0662×** |
| kernel/events/core.o | 1875.233 | 1751.404 | **1.0707×** |
| mm/memcontrol.o | 1646.789 | 1581.615 | 1.0412× |
| fs/ext4/fsync.o | 488.609 | 495.128 | 0.9868×（单组轻微回退） |
| fs/ext4/super.o | 1099.972 | 1036.078 | 1.0617× |
| **五个样本中位数之和** | **6516.238** | **6182.586** | **1.0540×** |

此结果说明 Core 名称扫描确实值得独立研究，但不是整个 Linux 的 5.4% 提速：小输入中的索引建表额外成本可抵消收益，五文件样本被刻意挑选为热点、不能代表全部 500/2064。**候选状态 `verified-local-screen / 500 pending`**，不进入生产主线；已启动原已有 perf500 Job 的 first500 完整 SHA-frozen single-run paired A/B [#38999084169](https://github.com/yituanxing/minic-toolchain/actions/runs/38999084169)，复用原 `linux-perf-frozen-corpus-differential-v1.py` 与 4-worker、balanced old/new，初筛五文件不再在该 run 中重复做 4x；其结果和可能的单文件回退仍需严格审计。**没有执行 2064 Linux+QEMU，禁止称之为已晋级或最终性能提升。**

**first500 全量性能门（已关闭本候选）：** [CI #38999084169](https://github.com/yituanxing/minic-toolchain/actions/runs/38999084169) **SUCCESS**；原有 4-worker balanced-order frozen500 配对与 SHA 校验，**500/500 完全相同 .s**，原版 `384357.214ms`，候选 `380694.509ms`，仅 **1.00962×**（单文件耗时累计下降 **0.953%**，节约约 3.66 秒）。单轮有 **206/500** 个文件 wall 较慢；中位单文件倍率仅 **1.00499×**。最大单轮绝对回退例 `fs/nfs/blocklayout/extent_tree.o` +419.117ms、`fs/nfs/client.o` +344.831ms，可能含明显调度噪声，**不能声称这两个是确定的编译器回归**。但当前候选的全量净收益 <1%，相对五个大 TU 初筛的 5.4% 明显稀释；既没有依据把 206 个单轮回退解释成全部真实，也没有必要在未晋级的低收益方案上花费 2064 全量+QEMU Runner。

**裁决：`verified-correctness-500 / low-gain-not-promoted / stop`。** 保留 `tools/ci/apply-perf-core-reachability-name-index-v1.py`、5-owner 和 500 的二进制/输入身份及原始 A/B，但**不修改生产 Core Reachability 源码、不晋级正式优化、不继续反复增加别名哈希/微调/全量验证**。此负结果不能否定另一种截然不同的、经新 CPU 数据证实的热路径改进。

**待验证卡 `core-reachability-name-lookup`**：先只在当前 frozen BTF / events-core 上计 `minic_core_named_function_id` 的调用量、总比较次数、失败比率及入口实际 CPU 成本，证明成本模型后才考虑 **per-Core reachability** 的只读 `symbol name → earliest function ID` 索引，严格保留 assembler_name 优先、大小写/长度、first-match、未知/OOM 原线性路径以及索引生命周期；不得创建跨 TU/全局静态未受限缓存。没有新证据之前不为新 Hash 直接触发 500/2064。

