# MiniC 性能候选决策索引（2026-10-17）

> **新优化实验先查此表，再查 [完整因果实验账本](linux-performance-causal-investigation-v1.md) 和 [回退复核](minic-compilation-regression-triage-2026-10-17.md)。**
> 本表是候选的**状态索引**，不是覆盖旧的原始数据；旧实验报告、补丁、CI artifacts 均保留。每次新优化先记录「相同结构以前试过没有、当时基线是什么、为何现在仍值得做」。
> 基线：主线 `6612e2c1a3`（文档头），最新已认证的 Boolean/Core/Integer 联合源码经 [2064 + GNU link + QEMU P1 #38995964988](https://github.com/yituanxing/minic-toolchain/actions/runs/38995964988) PASS。这个 PASS 是 GNU PP → MiniC → GNU AS/LD，不能声称全 Mini 工具链共同通过。下列不同实验的加速比不能相乘，也不能伪称当前 2064 的同比速度。

## 空间方向第三轮：按实际数量单独分配 Function 参数类型（2026-10-17）

**单因素新模型，先只改临时 CI 工作树，正式生产 C 尚未改。** 继前两轮 `MinicType` 72B 和 CALL 参数 ID 32-bit 后，七文件原样追踪 [#39007018275](https://github.com/yituanxing/minic-toolchain/actions/runs/39007018275) 显示 85,327 个 Function 中 77.76% 参数数≤2、均值1.770，然而每个 `MinicFunction` 依然 `MinicType parameter_types[64]`，单靠该数组固定占 4608B、整个 Function 6016B、16,384 容量的重型 TU Function 数组 **94 MiB**。虽原讨论首先考虑“小内联4 + 超额另存”，但存在 **by-value Function snapshots + Function 数组 realloc 后内联指针悬空**，因此先用**更保守的私有 exact-length heap 参数数组**验证真实收益上界（而非冒险直接上 hybrid）；`MinicFunction` 保持原字段直接下标访问的语法，单个 Function 通过 `set_function_signature` 只分配实际参数数 `n*72B`，返回前 deep copy，析构各自 free，源 Function 被克隆到 specialized 时也通过现有 setter deep-copy，常见按值快照仅借用不可释放的参数数组。0 参数不分配，但非零参数每个 Function 添加 malloc，这项**新分配成本必须在真实 CPU/Wall 测试中检验**，不允许将减少 struct 字节当作真实收益。

**五个 SHA 固定 Linux owner A/B [#39008785116](https://github.com/yituanxing/minic-toolchain/actions/runs/39008785116) SUCCESS**：同 runner，每臂 4 次交错，`make check-ast-contract` PASS；旧/新实际 `sizeof(MinicFunction)=6016→1416B`，字段宽度 `sizeof(MinicType)=72B` 不变，函数类型描述符未改，Specialization fixed fact arrays 未动；Linux 输出 `.s` **5/5 byte-identical**。代表性每 owner 的 old P50 → new P50：
- fsync **338.417→305.659ms (1.10717×)**；峰值 RSS 单进程 **182372→137688 KiB**；
- BTF **992.052→903.015ms (1.09860×)**；RSS **306592→243728 KiB**；
- events/core **1359.769→1200.996ms (1.13220×)**；RSS **289216→231772 KiB**；
- memcontrol **1192.284→1060.027ms (1.12477×)**；RSS **285756→227712 KiB**；
- ctype 2.670→2.521ms，短命毫秒级噪声意义低。
**五 owner 中位编译时间相加 3885.191→3472.217ms，1.11894×（累计用时减少10.63%）**。这些是特定 hot owner，不是任意 500 TU 或七路 Kbuild speedup；一次进程 RSS 不能直接称全 Linux 内存下降。

**扩大 frozen first500 [#39008901495](https://github.com/yituanxing/minic-toolchain/actions/runs/39008901495)**：使用现有同-runner 4-worker balanced differential，500/500 `.s` 必须 byte-identical，真实累计耗时/回退与 8×复测以后，才讨论正式合入；本阶段无新生成 production `src` 代码，无 2064 Linux+GNU Link+QEMU P1 新认证。若首次 sample 正面且有显著收益，将进一步判定是不是值得承担 per-Function malloc 与签名 setter 语义变化；若回退则审查 malloc 热点，考虑稳定 arena/合适 hybrid，不无故升级到 full run。

## 空间方向第二轮：MinicType 字段排布，消除 ABI 对齐空洞（2026-10-17）

**单因素白盒发现：** `src/frontend/type.h` 的 `MinicType` 在 64-bit 平台占 **80 bytes**，原顺序 `base_kind` 后紧跟四个 `size_t` ID 导致对齐空洞；将小型 Enum/qualifier/pointer 元数据字段放在前面，四个 ID 和 `explicit_alignment` 放在尾部，**不改变任何成员 C 类型、名称、数量、ID 上限、所有权、算法和数组扩容行为**，单是布局可降至 **72 bytes**，同时间接减小 `MinicFunction` 里的 64 个参数类型（约少 512B/Function）以及 `MinicFunctionType`、Expression 类型字段。与上一轮 `uint32_t CALL ID` **完全不同的新成本模型**，不同于已被否决的 `PERF-033` 扩容倍率方案。实验只在临时 CI checkout 应用 `tools/ci/apply-perf-type-field-layout-v1.py`，生产源暂未改。

**局部真实 Linux A/B [#39005831148](https://github.com/yituanxing/minic-toolchain/actions/runs/39005831148) SUCCESS**，在最新 packed-CALL 生产源上，5 个真实冻结 Kbuild `.i`，同 Runner 4 对交错 old/new，**5/5 `.s` byte-identical**，`make check-ast-contract` PASS，`sizeof(MinicType)` 80→72 已经通过真实 host `cc` 输出实测：

| owner | old median ms | new median ms | speedup |
| --- | ---: | ---: | ---: |
| fs/ext4/fsync.o | 275.055 | 266.980 | 1.03025× |
| fs/ext4/super.o | 604.674 | 596.248 | 1.01413× |
| kernel/bpf/btf.o | 784.442 | 750.421 | 1.04534× |
| kernel/events/core.o | 1053.955 | 1039.189 | 1.01421× |
| mm/memcontrol.o | 931.432 | 902.807 | 1.03171× |
| **sum of owner medians (not parallel wall)** | **3649.556** | **3555.644** | **1.02641×** |

一次运行的真实峰值 RSS KiB：fsync 189368→180524，super 227764→217408，btf 321656→306184，core 297984→287124，memcontrol 292856→281156。每个例子都下降，不能把一组单独 sample 当作 RSS 置信区间，也不能将整个 Linux 内存下降 4%。

**完整 first500 晋级 A/B [#39005940554](https://github.com/yituanxing/minic-toolchain/actions/runs/39005940554) SUCCESS**：500/500 `.s` byte-identical，同一 runner、4 workers、250/250 balanced order，当前生产 packed CALL IDs baseline 累计 **348126.483ms**，Type 72-byte candidate **341344.660ms**，**1.01987× 加速，累计时间 -1.948%（节省 6.781823 秒）**。单 TU 倍率中位数 **1.02211×**。单次 500 中 **107/500** owner wall 看似回退，因此没有把局部 5/5 结果直接当成生产胜利，单独进行了重复复核。

**10-owner CPU/user/wall 回退排查 [#39006242085](https://github.com/yituanxing/minic-toolchain/actions/runs/39006242085) SUCCESS**：从 first500 选取 8 个最大单次时间回退 owner + BTF/fsync 两个收益对照；沿用现有 `linux-perf-packed-call-regression-review-v1.py` 的 paired 4×2 批量机制（只是新增可复用 `--owner`/`--label`），每个 owner 8 old/8 candidate，wall/user/sys/MAD 都留原始 JSON。**10/10 汇编完全一致，10/10 重测 wall P50 不慢于原版，0 个重复确认的 wall+CPU 回退嫌疑**。例如 `fs/nfs/client.o` first500 单次 814.762→1116.782ms 看似恶化 +302.0ms，重测 P50 **801.930→789.090ms**、user CPU **737.616→721.362ms**；`fs/nfs/blocklayout/extent_tree.o` 重测 **752.963→746.643ms**；`mm/compaction.o` **652.976→652.418ms**，收益较小。因 total -1.95% 略低于默认 2% 候选阈值，但该方案是 1-header 无语义改变的布局调整，且 5 个文件实际 RSS **全部下降**、10 个单次回退重复复核通过，所以作为谨慎例外晋级实际完整运行时验证；不能把 107/500 的单轮回退自动解释为负优化，也不能宣称 107 个都已复测。

**正式类型头文件已合入，且完整 Runtime 已通过**：[Production commit a6186876](https://github.com/yituanxing/minic-toolchain/commit/a6186876c684b0cbe945b77dc5c96ebce6f6d6fb)，单文件 `src/frontend/type.h` 按实验一致字段排列，`sizeof(MinicType)` 实测 **80→72B**，`sizeof(MinicFunction)` **6536→6016B**（单 Function -520B），`sizeof(MinicFunctionType)` **5216→4696B**，`sizeof(MinicExpression)` **424→416B**；无额外 malloc、无 ID 类型或语义改动。已触发 [Focused T1 #39006469220](https://github.com/yituanxing/minic-toolchain/actions/runs/39006469220) 和 [Linux 7-shard + GNU/QEMU #39006469186](https://github.com/yituanxing/minic-toolchain/actions/runs/39006469186)，两条最终均 SUCCESS。**最终验收已通过 [CI #39006469186](https://github.com/yituanxing/minic-toolchain/actions/runs/39006469186) SUCCESS**：7/7 Linux C shards、`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`、GNU Link `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`、QEMU `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`。[Focused T1 #39006469220](https://github.com/yituanxing/minic-toolchain/actions/runs/39006469220) SUCCESS：`TOOLCHAIN_FOCUSED_T1=PASS independent_test_checks=8`。**最终决策：`minic-type-field-layout-72-v1 = integrated / runtime-certified / do-not-repeat`**。该验证链是 `GNU PP → MiniC → GNU AS/LD → QEMU`，不可冒充此次完整自制工具链组合测试。

## 空间方向第一轮：固定 64 CALL 参数槽使用 32-bit ExpressionID（2026-10-17）

**先做一张低风险单因素卡，不直接搬数据到 heap。** 当前 `MinicExpressionId` 是 `size_t`，但单 TU AST ID 实测最多 22 万级，< 2^32；`MinicExpression` 内嵌 64×8 字节参数槽，甚至 93.65% 非-CALL 都承担这些联合体字节。之前的 `PERF-033` 是尝试 2×→1.5× **数组扩容因子**，本次**不碰扩容**、也不改变参数个数/顺序，不增加 malloc/arena 对象：仅把 CALL 内嵌 64 个 AST ID 压成 `uint32_t[64]`，setter 对溢出显式检查、AST 添加新表达式超过 UINT32_MAX 时 fail，遍历 remap 保持 `size_t` 局部临时及回写检查。注意 32-bit ID 表示对理论上超过 UINT32_MAX 的单 TU Expression ID 引入了明确的拒绝条件；**初筛只在一次性 CI checkout 生效，待 500、极值参数和 runtime 验证都通过后已作为独立生产优化合入**，详见本节最终验收。

**局部真实 A/B #39002084966 [SUCCESS](https://github.com/yituanxing/minic-toolchain/actions/runs/39002084966)**：原 Runtime-certified MiniC 编译为 baseline，在临时 CI 工作树应用 `tools/ci/apply-perf-ast-packed-call-ids-v1.py` 生成 candidate，`make check-ast-contract` PASS，原有 frozen Linux 6.6.143 `.i` **4/4 完全相同汇编**，同 Runner 各四次 old/new 交错。
  
| owner | old P50 ms | candidate P50 ms | 加速 |
| --- | ---: | ---: | ---: |
| arch/riscv/purgatory/ctype.o | 3.352 | 3.230 | 1.038×（3ms tiny TU，结果不能过度解读） |
| fs/ext4/fsync.o | 451.610 | 426.582 | **1.059×** |
| kernel/bpf/btf.o | 1295.356 | 1252.876 | **1.034×** |
| mm/memcontrol.o | 1521.468 | 1456.711 | **1.044×** |
| **合计** | **3271.785** | **3139.399** | **1.04217×** |

单独的一次 OS RSS 进程观测：fsync 248,580→192,580 KiB（-22.5%）、btf 425,996→316,004 KiB（-25.8%）、memcontrol 397,328→292,636 KiB（-26.3%）；这是 peak RSS **一次运行**，尚不是系统内存压力或全量 RSS 的有统计把握的推断。没有实测 `sizeof(MinicExpression)` 新值时，不将“理论减少 256 bytes”伪装成 sizeof/RSS 认证。

**冻结 first500 晋级门已经 SUCCESS**：[CI #39002211001](https://github.com/yituanxing/minic-toolchain/actions/runs/39002211001)。使用旧版与 candidate **完全相同**的两个编译器构建配置，同一 Runner、4 worker、250/250 balanced execution order，全部 **500/500 Linux Kbuild .i 输入输出 `.s` SHA 完全相同**：旧版累计 **289603.901ms**、packed-ID 版 **269198.125ms**，**加速 1.07580×、累计耗时降低 7.045%、节省约 20.406 秒**；单 TU 倍率中位数 **1.08192×**。这不能直接当作 7-worker Kbuild 总 Wall 时间提速。同一 500 文件里 candidate **7/500** 单轮 wall 比旧版慢，Top3：`fs/nfs/client.o` +131.536ms、`fs/nfs/blocklayout/extent_tree.o` +65.376ms、`init/version.o` +65.055ms；之前类似单次 Runner 回退大量由后续 CPU/user 多轮复核逆转，应保持原始清单，不直接定性为算法回归。

**当前已晋级生产实现（完整 2064 QEMU 审核另计）：** Packed CALL-ID 原先的 temporary opt-in 方案在 [合入 commit 5cb252e9](https://github.com/yituanxing/minic-toolchain/commit/5cb252e918112337d4e281267a0349ad490a4b55) **六个正式 C/H 文件原样落地**，保持 64 参数槽 inline、结构体按值拷贝、0 新的逐 CALL heap malloc；参数 ID `uint32_t` 的写入和 AST traversal/remap 回写均有边界检查；`minic_c0_program_add_expression` 拒绝生成不能表示的 ID。新的正式头文件 SHA `5b6eb641d73e67d7b271c54aca180e4e587deac3` 已更新到当前 native profile manifest，非旧版历史认定；先前 SHA 不匹配导致第一次七路 run [#39002613400](https://github.com/yituanxing/minic-toolchain/actions/runs/39002613400) 7/7 fail（**源码身份门禁拒绝**，不是 2064 Linux 编译差异），按正确 pin 重跑 [#39002638516](https://github.com/yituanxing/minic-toolchain/actions/runs/39002638516) 当前已确认 7/7 shard SUCCESS，而且 **最终 aggregate Job 和整个 workflow 均 SUCCESS**：`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`、`COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`、`COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`。QEMU Image 冻结证据 artifact `12089622387`。**正式状态：`runtime-certified / integrated / keep`**。仅证明 GNU PP→MiniC→GNU AS/LD 的编译器单替换，而非本次全 Mini 原生工具链联合回归。

**其它正确性资格：** [Focused T1 #39002613399](https://github.com/yituanxing/minic-toolchain/actions/runs/39002613399) SUCCESS，`TOOLCHAIN_FOCUSED_T1=PASS independent_test_checks=8`；AST contract local CI [#39002084966](https://github.com/yituanxing/minic-toolchain/actions/runs/39002084966) PASS；合成边界 [#39002498179](https://github.com/yituanxing/minic-toolchain/actions/runs/39002498179) SUCCESS：old/candidate 均支持 0/1/2/8/9/63/64 参数与 GNU cleanup、`.s` 相同，65 参数均拒绝，原始合成 fixture 第一次失败是前导未预处理 C 注释影响双方 parser，已清理后 PASS。**仍不可将 UINT32_MAX expression ID 越界合同变化称为无影响，需保留文档边界。**

**7 个单轮 500 outlier 复核已得到可用证据**：[CI #39002748529](https://github.com/yituanxing/minic-toolchain/actions/runs/39002748529) 对 7 个原单轮变慢 + 两个正对照各执行 old/new 8 次交错，9/9 `.s` 相同，`summary.json` artifact ID `12089693082`；**9/9 wall 中位数比原版快，0 个持续 wall+CPU 回退嫌疑**。原 `fs/nfs/client.o` 第一次单次 702.007→833.543ms，看似候选回退 +131.5ms，重复复核 P50 **861.140→820.186ms**、user CPU **784.164→752.420ms**；`fs/nfs/blocklayout/extent_tree.o` 重测 807.934→766.332ms；`init/version.o` 重测 95.047→85.639ms。**这条 CI job 最终状态仍是 FAILURE**，根因是 `tee` 记录最终回归摘要时没有预先创建 `$root/evidence` 路径（输出指标已经由 9 个 JSON 成功落档，日志有 `MINIC_PACKED_REGRESSION=PASS`）；已在 [CI 工作流修复 6665be9f](https://github.com/yituanxing/minic-toolchain/commit/6665be9ff95e87f861a5b1a7a80b76296221f6ad) 纠正，只修证据目录，**不浪费 Runner 再重跑已经实际验证的 9×8 pairs**。不能将此 FAILURE 伪称整条 GitHub CI SUCCESS，但也不能把 log/JSON 的真实性能结果视作“没有跑”。

## 最新实测 AST 存储决策（同一已认证 MiniC，2026-10-17）

**实测输入，不是猜测：** [AST 结构与 RSS 七文件 census #39001510651](https://github.com/yituanxing/minic-toolchain/actions/runs/39001510651) **SUCCESS**，`.s` 7/7 一致，原版/只读探针实际 GNU frozen Kbuild 文件。当前 `sizeof(MinicExpression)=680`、`sizeof(MinicFunction)=6536`、`sizeof(MinicFunctionType)=5216`；7 个 Linux 编译的 **1,158,376** 条表达式中，只有 **73,572 CALL（6.35%）**，超过 8 个参数的 CALL 总共只有 **26** 条；所有表达式静态预留 **64×8=512 字节** 的 CALL 参数槽。BTF 的 Expression 容量名义 **170 MiB**、Function 容量名义 **102.12 MiB**，实际原编译器 peak RSS **418.4 MiB**；其它文件见 [完整独立审计](minic-ast-memory-shape-census-2026-10-17.md)。旧版 `apply-perf-ast-footprint-trace-v1.py` / `linux-perf-ast-footprint-v1.sh` **已存在**；本次新记录是在最新 AST 优化后的 7-TU、两阶段、CALL histogram 和 OS RSS 扩充，不能把已有 footprint profiler 重新发明成一项新优化。

**后续候选队列（防重复）：** `ast-expression-packed-call-id-v1` **已生产合入 / runtime-certified**，不准重新包装成新空间优化。与历史 `PERF-033` 的 2×→1.5× 数组容量调整完全不同，后者仍是 **rejected**。现在可以在新 baseline 上检查 `ast-function-cold-parameter-facts`（每个 Function 约 6536 bytes）是否值得独立尝试；但 Function 列表/参数事实克隆路径风险高，仍需先量 Function 真实类型分布和布局空间贡献。进一步从 inline 256-byte CALL 参数槽改为外部 arena 很可能重新引入 malloc/访存/ownership 成本，**没有新的当前 Callgrind 和真实 RSS/CPU 收益证据就不自动再尝试**；`layout-repeated-record-work` 需更新 CPU evidence。

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

**52 个初次回退观测的复核现已闭环**：[CI #39000717601](https://github.com/yituanxing/minic-toolchain/actions/runs/39000717601) **SUCCESS**。针对 500 A/B 中单轮变慢幅度最大的 8 个真实 Linux 文件，以及 3 个预期获益的控制样本，使用与 first500 完全相同的 **old `minic-cc` SHA `3312167e...` / candidate SHA `96e8f2dc...`**、同一 SHA-locked GNU Kbuild .i，各臂 **8 次交错有效测量（额外排除预热）**，逐次记录 wall、user、sys CPU 和 MAD；**11/11 汇编完全一致，11/11 的 wall 中位数都更快，而且 CPU user 中位数也全部下降；0 个确认的 wall+CPU 持续回退，0 mixed/noisy**。典型翻转：`fs/nfs/client.o` 首次单轮 **894.984→1020.799ms（+125.815ms）**，重复复核中位 **895.928→858.225ms（-37.703ms）**、user CPU **813.101→784.541ms**；`kernel/events/core.o` 首次 +45.706ms，重复复核 **1753.345→1708.789ms（-44.556ms）**、user CPU **1633.684→1595.091ms**。这是**优先挑出的 11 个样本**，不能由此宣布另外所有未复核 41 个负样本绝无回退，但足以排除最显著 8 个观测即确定性回退的解读。

本轮还对正式 `src/frontend/ast_verifier.c` 执行了真正的 `make check-ast-contract`，日志 `MINIC_AST_ARRAY_CONTRACT=PASS`；诊断重建生产源前的原版 AST blob **`f3e023c6...`** 与 first500 old/new 二进制 SHA 双重锁定，`MINIC_AST_RECHECK_EXACT_BINARIES=PASS`。综合结论：**正式保留这个新算法；500 全体 A/B 获得约 4.05% 总编译耗时下降、2064/2064 GNU 链接及 QEMU P1 通过，回退风险最大的真实 Linux outlier 经 CPU clocks 复查未重现。** 不为证明每一个 CI 单轮扰动再强制重编 2064。

## AST Owner 正式入库后重新做 CPU 归因（2026-10-17）

**新版本真实 Callgrind 已 PASS**：[CI #39001011273](https://github.com/yituanxing/minic-toolchain/actions/runs/39001011273)；在原有 perf500 的 CPU profile-only 工作路径复用原 `fs/ext4/fsync.o` 与 `kernel/bpf/btf.o` 两份相同 SHA-锁定输入，`bin/minic-cc` Debug 代码加入只读 Parser-copy census，**2/2 `.s` 与新 Release 原样逐字节相同**，没有改变生产源码或运行时行为，工具仍用 Valgrind Callgrind（**Ir 是模拟指令量，不是墙钟时间**）。与原旧版本 [#38997549409](https://github.com/yituanxing/minic-toolchain/actions/runs/38997549409) 同输入的指令自耗比较：

| Owner | 原总 Ir | 新总 Ir | 减少 | AST verifier 函数 self Ir（原→新） |
| --- | ---: | ---: | ---: | ---: |
| `kernel/bpf/btf.o` | 10,233,638,583 | 9,995,059,298 | **2.331%** | **271,512,020→33,430,614** |
| `fs/ext4/fsync.o` | 3,750,494,966 | 3,641,445,729 | **2.908%** | **128,933,064→20,189,344** |

以上是**同一工具可复算的指令事件**，不是严格的两版本 wall-time A/B（500 实测耗时减少 4.05%，另有各自测量噪声）。两个 input 下，`__memcpy_avx_unaligned_erms` 的自耗和 `minic_specialize_inline_symbolic_calls` 的绝对 Ir 都基本不变；它们占**剩余**指令比例看起来上升，原因包含 AST 校验指令减少带来的总分母缩小，**不是新版本出现这些函数退化**。BTF 当前 self Ir 约：memcpy 29.11%、Symbolic 10.23%、Enum bind 6.57%；fsync 当前 self Ir 约：memcpy 45.87%、Symbolic 7.32%。旧 Parser 复制点/Enum 重名 Hash/CORE 名称查找已经在本账本中独立试过，被限制或否决，**不得单凭现有 self 占比又重复启动它们**。

**下一轮诊断优先队列（不直接改算法）**：现阶段剩余耗时一部分来自 `MinicExpression` 内嵌 64×8-byte CALL 参数数组的整体存储放大，历史白盒 audit [whole-path cost audit](minic-i-to-s-whole-path-complexity-audit-2026-10-15.md) 说明静态每个 Expression 的 union 至少 512B，但是**尚缺 Linux 当前真实输入的 Expression/Function 大小、各类 kind 分布、真实 RSS、内存分配/扩容耗时**。这是不同于已经被拒绝的 Parser `memcpy` 复制问题：实际拥有 O(expression_count) 的 AST 大结构，而 Parser 是少量大结构快照。**先用低开销 census 和带 SHA 的同输入统计数验证潜在绝对收益，再决定是否触碰高风险 AST Representation**。其它次选 `src/target/data_layout.c` record/layout 重算，已有旧白盒审计指出 Codegen 的双遍扫描，应先量局部布局调用次数和当前阶段 CPU，而非重新跑 P14/P15。

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

