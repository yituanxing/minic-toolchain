# MiniC 性能候选决策索引（2026-10-17）

> **新优化实验先查此表，再查 [完整因果实验账本](linux-performance-causal-investigation-v1.md) 和 [回退复核](minic-compilation-regression-triage-2026-10-17.md)。**
> 本表是候选的**状态索引**，不是覆盖旧的原始数据；旧实验报告、补丁、CI artifacts 均保留。每次新优化先记录「相同结构以前试过没有、当时基线是什么、为何现在仍值得做」。
> 基线：主线 `6612e2c1a3`（文档头），最新已认证的 Boolean/Core/Integer 联合源码经 [2064 + GNU link + QEMU P1 #38995964988](https://github.com/yituanxing/minic-toolchain/actions/runs/38995964988) PASS。这个 PASS 是 GNU PP → MiniC → GNU AS/LD，不能声称全 Mini 工具链共同通过。下列不同实验的加速比不能相乘，也不能伪称当前 2064 的同比速度。

## 第七轮诊断开卡：Symbolic 剩余成本分解（2026-10-17）

- **状态：诊断进行中，尚无新 A/B 性能结论。** 基准为第六轮 Linux 2064/QEMU P1 已认证生产 C（`e8fef972` 的正式 C 与 `cbbd5654` 一致），不修改生产 `src/`，不建新 workflow/job，不重编 2064，仅复用 first500 缓存中的既有 20 个 stratified SHA-frozen Linux `.i`，使用已有 `perf500` job 和原始 phase-trace 工具。
- 对 `minic_specialize_inline_symbolic_calls` 只读插桩：direct/transitive 的 expression visits、CALL hits、source-owner skips、pointer argument evaluations、variant lookups/probes、clone 总数；直接使用已有 `apply-perf-symbolic-loop-trace-v0.py` 的两个分段计时。计数不是 CPU 周期，插桩 wall 不是正常速度，不据此宣称加速。
- **历史防重复门槛：** Symbolic Source Owner 索引已合入，`PERF-023` 普通 CALL-only 索引在 #38665476648 局部约 -5.15%，但未晋级；本轮不会再次直接尝试这个旧结构。若 owner skips/variant probes 当前真实工作量不足，不做新的 Symbolic 索引，转看 Parser/Lexer/Core 最新热点。
- 首次诊断 [#39020963620](https://github.com/yituanxing/minic-toolchain/actions/runs/39020963620) 在**旧版 Boolean census 的过期源码匹配锚点**失败，未进入 20 TU；第二次 [#39021055244](https://github.com/yituanxing/minic-toolchain/actions/runs/39021055244) 已成功构建 opt-in Symbolic 仪表，但仅因 phase-trace Python self-test 正则转义笔误失败，未进入 20 TU。这两个均不是 MiniC 正式源码编译/QEMU 回归；现已纠正且保持旧 Boolean instrumentation 不参与本次 Symbolic 测量。
- 晋级条件：20/20 原始 vs 计数插桩汇编字节一致、计数满足关系不变量、分段时间有来源，且出现**历史未覆盖**的显著真实成本，才建立具体的单因素 A/B。只有通过同 Runner wall/user/sys/RSS、回退复核，才考虑扩大到 500 和 2064/QEMU。

### 第七轮诊断结果（当前生产 C，2026-10-17）

**正式有效验证**：[现有 perf500 CI #39021486485](https://github.com/yituanxing/minic-toolchain/actions/runs/39021486485) **SUCCESS**；复用原 500 SHA-frozen GNU Kbuild 缓存的固定 20 TU（hot10/middle5/light5），**20/20 原始/诊断 `.s` 字节严格一致**，没有优化候选和生产 `src` 修改。Direct/Transitive 计数沿用原 `apply-perf-symbolic-loop-trace-v0.py`，新计数只统计循环工作；独立 `minic-cc` CPU Callgrind fsync 已通过 `MINIC_HOTSPOT_PROFILE=PASS`、汇编一致。旧 Boolean census patch 已不兼容现版 Boolean local-domain helper，本次明确 SKIPPED；其原始 `spec-boolean` 外层计时仍有效，不能伪称 Boolean direct/transitive 已重新测出。

| 已验证 20 TU Symbolic 计数 | 累计值 |
| --- | ---: |
| Transitive specialized callers（进入循环的 clone） | **3,283** |
| Transitive Expression 内循环迭代 | **603,761,021** |
| 经 Owner Index 明确排除其他 Source 的访问 | **472,979,276（78.34%）** |
| 未被 Owner 过滤的 Expression 访问 | **130,781,745** |
| 留在循环内的 CALL visits | **885,165（未排除访问中的 0.677%）** |
| Transitive pointer argument evaluation attempts | **281,367** |
| Transitive variant lookup / linear candidate probes | **390 / 4,871,138** |
| Direct variant lookup / linear candidate probes | **898 / 11,050,065** |
| Owner Index ready / fallback（按 TU 计） | **20 / 0** |
| 新增 Direct / Transitive clones | **898 / 390** |

同 Runner 20 TU 本次**插桩**阶段计时：Symbolic Direct **76.501ms**、Symbolic Transitive **1460.341ms**，Parser **3729.559ms**，Core-set **1954.011ms**，全部诊断编译累计 **11303.688ms**。这些是 **选中的 20 个 TU** 的 opt-in tracer wall，不是 2064 全量统计，也不是未插桩 A/B；前一轮仅 workflow 过期 assertion 失败但 20/20 内部通过的运行 #39021371976 给出 Symbolic Transitive **1450.590ms**，全诊断 wall **8015.250ms**，说明 Runner 环境和插桩其他阶段变化较大，不能从比例推断 CPU 周期或性能收益。

**新候选初判：** 核心可避免工作是 Transitive 的 `clone × 全 TU expressions` 扫描。已集成 Owner Index 仍在 603.76M 次循环里做事后过滤；只留下 0.885M CALL visits。下一张卡如果进入 A/B，应与已拒绝的 `PERF-023` **全局 CALL-only** 不同：考虑**source-grouped CALL visit buckets + ambiguous/shared fallback**，同时消除非 CALL 和其他 source 的 CALL 遍历。必须保留原 `ExpressionId` 升序、共享/不明 owner 扫描、新增 Expression 处理、在 OOM 下退回原逻辑、clone 及 Variant first-match 顺序；构建索引的 CPU/RSS 也必须计入 A/B。先采用 3～7 个正负热样本验证，不直接 500/2064。

备选路径：Variant lookup 共有 **15,921,203** 次线性 candidate probes（1,288 次 lookup，平均约 12,361 probes），具备按 source_id 筛除无关变体的潜力，但目前缺少同阶段中这部分的**独占 CPU 成本**，不可直接认定它比 603.76M expression 迭代更值得优先修改。第三个观察是 Parser 仍在这一组样本中耗时更高，Symbolic 若独立 A/B 无足够边际收益应立即转回 Parser/Core，而非无限细抠本阶段。

### 第七轮 Source-grouped CALL 正式源码晋级及运行时验证（2026-10-17）

**测试前置完整：** 单因素冻结 first500 [#39022225132](https://github.com/yituanxing/minic-toolchain/actions/runs/39022225132) 的 500/500 `.s` 一致、累计 **1.07605×**（254748.100→236743.979ms）；从 57 个初次单轮回退选最差 8 个+3 对照，[#39022601919](https://github.com/yituanxing/minic-toolchain/actions/runs/39022601919) **11/11 同 Runner CPU/wall P50/MAD，无确认持续回退**。只证明采样范围，绝不宣称全部初次回退已逐个复核。

**正式安全门禁：** [现有 perf500 #39023367899](https://github.com/yituanxing/minic-toolchain/actions/runs/39023367899) **SUCCESS**。9,000 个确定性 reference-model 测试覆盖唯一 Owner、共享/不明 Owner、升序双桶归并和 Late Expression 动态追加；4 个 SHA-frozen Linux `.i` + 4 个 positive/negative fixture，**8/8 × 5 编译臂（基线、候选、强制 Bucket OOM 等价回退、强制 Owner Index OOM 等价回退、ASan/UBSan）全部按预期接受或拒绝；接受的产物严格字节相同**。在候选和 sanitize profile 上 `check-ast-contract check-type` PASS。首次 #39023196328 因负例在 Parser 阶段先拒绝而无法达到 Symbolic 测试钩子，诊断脚本的“必须命中钩子”断言错位；实际全部 Linux/正例通过，修正断言后重跑 #39023367899 通过。这是**测试辅助错误**而非编译器失败。ASan/UBSan 仅证明本次覆盖路径，非全路径形式证明。

**唯一生产更改：** [compiler.c 提交 7ed87186](https://github.com/yituanxing/minic-toolchain/commit/7ed87186187162e15efb274a8e24be57b5a65599) 从**完全同一份成功 A/B 的四处唯一锚点**提升 Symbolic Source-grouped CALL Index，不包含任何 fault-injection 测试钩子，保留旧扫描 OOM fallback；C Blob SHA `3b6df766a05bcaab365289b39e414fc9be8c2ba3`。紧接着 [Manifest 提交 8e6542e8](https://github.com/yituanxing/minic-toolchain/commit/8e6542e8237546c36c9070ebc71fb7d0b1b0b382) 将该 SHA 作为正式 Runtime 源码闸门。

**完整运行时最终正式验证：** [#39023546906](https://github.com/yituanxing/minic-toolchain/actions/runs/39023546906) **SUCCESS**：7/7 GNU PP→MiniC→GNU AS shard PASS，Receiver 精确合并并复用 2064 对象，GNU Link `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`；Linux 6.6.143 Image 经 QEMU P1 **`COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`**；`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`。Focused T1 [#39023526235](https://github.com/yituanxing/minic-toolchain/actions/runs/39023526235) **SUCCESS**。**最终裁决：`symbolic-source-grouped-calls-v1 = integrated / runtime-certified / keep / do-not-repeat`。** 运行时门禁是 GNU PP→MiniC→GNU AS/LD，不代表这一轮所有 Mini 原生工具组合。

### 第七轮 source-grouped Symbolic CALL 索引：500 汇编等价、回退复核完成（2026-10-17）

**与旧 PERF-023 的区别：** 旧版仅建立全局升序 CALL-ID 列表，依然要让每个 Clone 扫描所有 CALL；本版是 **Source Function 分桶 CALL + 共享/未知 Owner 保守兜底桶**，两个桶按 ExpressionId 稳定有序合并，新的 Expression 按原逻辑处理，任何分配失败都回退旧完整遍历。候选 `tools/ci/apply-perf-symbolic-source-bucket-calls-v1.py` 只用于 CI 临时 checkout，不修改正式生产 `src/compiler/compiler.c`。

**五个真实 owner 初筛：** [CI #39022058658](https://github.com/yituanxing/minic-toolchain/actions/runs/39022058658) 里候选 `MINIC_SYMBOLIC_SOURCE_BUCKET_AB=PASS`，5/5 `.s` byte-identical，累计 P50 **3789.997→3332.833ms / 1.13717×**；但 Job 最终 FAIL 是同 Job 中旧 Parser smoke 误依赖未构建的 `baseline-toolchain/bin/minic`，不是候选编译不通过。后续 [#39022225132](https://github.com/yituanxing/minic-toolchain/actions/runs/39022225132) 五文件重测 **3855.602→3398.774ms / 1.13441×**，4/5 快，1 个微型 owner（kernel/printk/sysctl）几乎持平。这两个数字来自不同 runner，不能叠加。

**真正 first500：** [CI #39022225132](https://github.com/yituanxing/minic-toolchain/actions/runs/39022225132) 原来 SHA-frozen Linux 6.6.143 GNU Kbuild `.i`，相同 Runner、四 worker、balanced-order；**500/500 `.s` 全部字节一致**。基线累计 TU wall **254748.100ms**、候选 **236743.979ms**，累计节约 **18004.121ms（约 7.07%）**，**1.07605×**。单轮有 **57/500** 个候选 wall 比基线慢，不能简单忽略。这是累计单 TU wall，不是 7 路 Kbuild critical-path wall，不是 2064/QEMU。该 Job 最终 FAIL 是 unrelated legacy `Paired integrated optimization A/B` 访问未构建 baseline 路径；候选自己的五文件与 500 文件两步 **SUCCESS** 且保留 artifact。后续单独隔离了旧 Job 的不适用步骤。

**11 文件回退复核：** [现有 perf500 CI #39022601919](https://github.com/yituanxing/minic-toolchain/actions/runs/39022601919) **整体 SUCCESS**。独立重建相同生产 C+临时候选编译器，重用既有 `linux-perf-pinned-single-paired-ab-v1.py`，原 first500 中最坏的 8 个单轮负收益文件，外加 3 个正对照，每 arm 预热 + 4 次交错，输出再次严格 byte-identical；wall/user/sys P50+MAD 判别。**11/11 成功，0/11 复现有意义的 CPU+wall 同时回退**。例如原 500 单轮表现最差的 `fs/nfs/client.o` 复核 **1.11606×**，`fs/nfs/blocklayout/extent_tree.o` **1.12133×**，正对照 `kernel/events/core.o` **1.17672×**，`mm/memcontrol.o` **1.17026×**。其余 49 个单轮负收益尚未逐个复核，不能写成全部消失。

**RSS 没有统一下降：** 5-owner 独立单进程 /usr/bin/time 峰值 KiB：fsync 88984→89080、BTF 157024→154436、events/core 183020→182148、memcontrol 149064→150372、printk/sysctl 24788→25896。候选增加索引内存，不能宣称是统一节省空间的优化。

**最终状态更新：** 本节原始 500/500 与 11 文件诊断先于合入，现在 9,000 个索引次序模型测试、8×5 臂安全验证（#39023367899）、正式源码 7ed87186、2064 GNU 链接/QEMU P1 (#39023546906) 均已通过；`symbolic-source-grouped-calls-v1 = integrated / runtime-certified / keep / do-not-repeat`。后续不得以该相同索引重新抽卡。

## 第六轮完成后最新 CPU 归因：Parser hot-copy 病因解除，下一张卡需重选（2026-10-17）

**全量来源准确性：** 第六轮正式源码 [cbbd5654](https://github.com/yituanxing/minic-toolchain/commit/cbbd5654cc8f7fa508e798bc0262f49e4c32d3ee)，配对测试 500/500 byte-identical、**1.08697×（累积 wall 180349.471→165919.403ms, -8.00%）**、Sanitizer + 9 例 parser edge + 10-owner 8x 回退复核已通过。**完整 Runtime [#39016455728](https://github.com/yituanxing/minic-toolchain/actions/runs/39016455728) SUCCESS**：7/7 构建分片、`GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`、GNU Link `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`、QEMU `COHORT_P1=PASS syscalls=12 python_era_oracle=PASS`、`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`。Focused T1 [#39016455740](https://github.com/yituanxing/minic-toolchain/actions/runs/39016455740) SUCCESS。本轮最终标签：`parser-cold-switch-heap-owned-v1 = integrated / runtime-certified / keep / do-not-repeat`。

**新的真实诊断 [#39016792065](https://github.com/yituanxing/minic-toolchain/actions/runs/39016792065) SUCCESS**：对同一 SHA-frozen Linux `.i`，保留未修改生产 C 的 copy probe 和 debug Callgrind，2/2 `.s` byte-identical。第一次 #39016651501 profiler FAIL 是旧脚本误把 `sizeof(Parser)>=131072` 当不变量，在当前 1064B Parser 上报 `inconsistent parser copy census`，仅测试仪表过期，非 MiniC 本身的编译失败；修复后同一现行源码复测通过。

| Linux owner | 上一版（第六轮之前）的 Callgrind total Ir | 第六轮后 Callgrind total Ir | libc `memcpy` self Ir 占比前→后 | Parser 按值复制名义总量前→后 |
| --- | ---: | ---: | ---: | ---: |
| fs/ext4/fsync.o | 3,471,772,669 | **1,806,899,557** | **48.07%→0.50%** | **1.546 GiB→0.012 GiB** |
| kernel/bpf/btf.o | 9,632,237,372 | **6,760,025,836** | **30.19%→0.65%** | **2.667 GiB→0.021 GiB** |

注意 Callgrind 的 **Ir 是虚拟机指令计数，不是 wall-time、CPU 周期、DRAM 字节或真实 malloc 流量**。Parser copy 总名义值由固定 `sizeof(Parser) * calls` 算出，新/旧操作次数相同（fsync 12,430、BTF 21,446），缩小对象才是收益来源；避免把 48%→0.5% 当成整个程序性能提高 47.5%。同时 500 实测仅 -8% 累计 wall，更可信的产品表现数据仍来自 A/B。

**下一轮先选卡、不重复旧优化：** 现在的 fsync self CPU 热点为 `minic_specialize_inline_symbolic_calls` **13.73%**、`minic_lexer_next` **6.66%**，BTF 为 Symbolic specialize **14.48%**、`minic_parser_bind_enum_constant` **9.72%**、`minic_parser_span_equals` **6.22%**、Core named queue **4.17%**。原来对 memcpy 高占比的任何推断均已过期；下一阶段必须先查对应源代码的真实次数、算法上界和已知 rejected 方案，再选择单因素小范围试验。**历史 Parser 枚举 hash、Lexer 局部 lookahead 和 Core name-index 的低收益/拒绝案例不得无证复刻**。任何候选禁止在没有可复核 `.s` / wall / user CPU / RSS 之前进入生产源。

## 第六轮候选：Parser 冷 switch 容器外置，消除每个 lookahead 的 131KiB 冗余复制（2026-10-17）

**新 CPU 基线已经真正完成：** [Callgrind + Parser-copy census #39014430010](https://github.com/yituanxing/minic-toolchain/actions/runs/39014430010) SUCCESS，最新 `MinicExpression=208B`、已 2064/QEMU 认证的 MiniC release 与 debug/probe 编译器，复用原 SHA-frozen Linux `fs/ext4/fsync.o` 和 `kernel/bpf/btf.o`，**2/2 真实 `.s` 字节一致**。模拟的 Callgrind **Ir（指令数，不是 wall）**：fsync **3,471,772,669**，libc memcpy self **1,668,957,947（48.07%）**；BTF **9,632,237,372**，libc memcpy self **2,907,738,149（30.19%）**。Symbolic specialize self 分别 7.15% / 10.16%。Parser 快照精确 site 插桩：`sizeof(MinicParser)=133536B`，fsync **12,430 次 / 1.546 GiB 名义结构体复制**，BTF **21,446 次 / 2.667 GiB**；这些是 C 源值复制操作的总名义字节，**不是**实测 DRAM 流量，不可直接拿 memcpy 指令占比全部归因于 Parser。

**白盒发现的具体冗余与新成本模型：** `src/frontend/parser_internal.h` 的 `MinicParser` 里固定 `MinicParserSwitchContext switch_contexts[16]`，每个 context 存 512 个 `uint64_t` case lower 和 512 upper，再加 selector 元数据；这 16×8280=**132480B** 几乎支配全部 **133536B** Parser。即使只是 `probe=*parser` 来检查下一个 token，完整 cold switch 缓冲区都会跟着复制；`parser_string.c:689` 与 `parser_statement.c:2781` 是真实高频 site。此成本模型与以前已拒的 **仅替换 parser_string 两处 lookahead** 完全不同：本次单次移动 cold 数组使 `sizeof(MinicParser)=133536→1064B`，理论上所有现存 Parser 快照均受益，而不是逐个特殊重写调用点。仍然存在别的 source memcpy 消耗，应只用 A/B 实测判定收益。

**只在 opt-in disposable checkout 尝试**：`tools/ci/apply-perf-parser-cold-switch-context-v1.py` 只修改 `parser_internal.h`、`parser_statement.c`、`parser_function.c` 的 cold ctx 表示，在**第一次真正 switch**解析时分配唯一 16-slot case context 表，parser 主进程结束时释放；其它字段、case 64-bit 边界和 16 层嵌套仍保留，重新利用原 case 计数/OOM error 约束。不改变生产 `src`。**明确生命周期边界**：speculative Parser 值副本现在会借用 cold switch 指针，常见 Lexer-only probe 不写该表；若极端 GNU attribute/statement expression 探针可能写同一表，需在晋级生产前查证/添加测试或 COW 路径，不能仅以成功编译 500 正常 TU 宣称所有分支完全等价。

**5-owner 本地资格 [#39014923355](https://github.com/yituanxing/minic-toolchain/actions/runs/39014923355) SUCCESS**：`check-ast-contract check-type` PASS，嵌套 switch 和负数/64bit case 的合成样本 `.s` 逐字节相同，5/5 完全一样的 Linux `.s`，每臂四次交错：
| owner | old P50 ms | candidate P50 ms | 加速 |
| --- | ---: | ---: | ---: |
| fs/ext4/fsync.o | 272.966 | 222.228 | 1.22831× |
| kernel/bpf/btf.o | 863.910 | 771.683 | 1.11951× |
| kernel/events/core.o | 1161.705 | 1091.889 | 1.06394× |
| mm/memcontrol.o | 1013.198 | 944.735 | 1.07247× |
| arch/riscv/purgatory/ctype.o | 2.474 | 1.878 | 1.31736×（微小 TU、不可过度解读） |
| **五 owner P50 之和** | **3314.251** | **3032.413** | **1.09294×** |

本轮 RSS 单进程下降很少（BTF **155616→154444 KiB**），符合减少**反复 memcpy 流量**而不是长期保留 AST 容量的成本模型；不得宣传成整机 RSS 大幅下降。第一轮实验 [#39014850929](https://github.com/yituanxing/minic-toolchain/actions/runs/39014850929) FAILED 是误将依赖 frozen500 corpus 的实验 step 放在 cache restore 之前，未执行候选编译，与功能/性能无关；已移动该 step、在 #39014923355 实测通过。

**扩组 frozen first500 已 SUCCESS：** [#39015035562](https://github.com/yituanxing/minic-toolchain/actions/runs/39015035562)，原有同 Runner 四工作进程、250/250 balanced-order、SHA-frozen 500 个 Linux `.i`，**500/500 输出 `.s` byte-identical**。原版累计 TU wall **180349.471ms**，cold-switch 候选 **165919.403ms**，**1.08697× 加速，累计编译耗时下降约 8.00%，节省 14.430 秒**；单 TU 加速比中位数 **1.09069×**。**19/500 单轮较慢**，最差绝对回退记录包括 `kernel/time/vsyscall.o` 159.143→216.325ms、`fs/nfs/client.o` 499.570→554.227ms、`arch/riscv/kvm/vcpu_timer.o` 305.121→352.402ms、`arch/riscv/kvm/vcpu_pmu.o` 330.350→368.616ms、`kernel/bpf/cgroup_iter.o` 331.774→363.229ms。暂不能确定它们是并行调度噪声还是实际 CPU 退化，要重复 old/new 的 user/sys/wall/MAD，不能直接忽略。**候选状态 `first500-pass / regression-and-snapshot-safety-pending / production-not-modified`**，**不声称通过 2064/QEMU**；由于 `MinicParser` 值复制可能共享冷指针，正式合入前必须审计 speculative probe 是否会写 switch 表，明确生命周期和失败路径，必要时加 ASan/UBSan/异常路径定向测试。这里 8.00% 是 500 个 TU wall 耗时之和的下降，不能当作七路 Kbuild 总时间下降。

**最新回退复核 [#39015456847](https://github.com/yituanxing/minic-toolchain/actions/runs/39015456847) SUCCESS：** 19 个原 first500 单轮 wall 负值，选其中最大 8 个，加 BTF/fsync 正对照各 old/new **8 次交错**的 `wall/user/sys/MAD` 实测。**10/10 输出 `.s` byte-identical，10/10 wall P50 更快，0/10 复现持续 CPU+wall 同时回退**。例如 `kernel/time/vsyscall.o` 170.223→134.391ms、user 146.062→107.842ms；`fs/nfs/client.o` 708.380→627.476ms、user 651.990→570.793ms；`arch/riscv/kvm/vcpu_timer.o` 324.743→263.823ms。其余 11 个单轮负值未逐一复测，结论不覆盖所有极端路径。**候选状态已由 `first500-pass/regression-pending` 提升为 `first500-pass/10-owner-regression-pass/snapshot-lifetime-safety-pending/production-not-modified`**。

**当前白盒安全审查发现：** 现有 `MinicParser probe=*parser` 主要分布在 `parser_statement.c`、`parser_function.c`、`parser_record.c`、`parser_string.c`；就审查到的现有 probe 分支，主要调用 `minic_parser_advance`、常量词法判别、字符串长度和属性词法扫描，没有直接对 `probe.switch_contexts` 的写入。唯一 switch case 真实写入口是 `parser_statement.c:push_switch_context()` 与其分支归并函数，而正常真实主 Parser 在完成解析时统一释放。**尚未形成 ASan/UBSan 含异常诊断和 nested-switch 失败路径的正式安全证书，不能用 500 个成功 Linux TU 推断在 speculative Parser 共享指针/深度变动下绝无别名污染或释放问题。** 禁止在验证之前把该候选合入正式源/声称通过 2064+QEMU P1，后续阶段要补有针对性的所有权和恢复边界测试。

**第六轮安全闸门最终验收（合入前）：** [ASan/UBSan 三臂 #39016308108](https://github.com/yituanxing/minic-toolchain/actions/runs/39016308108) **SUCCESS**；测试明确重用 current certified GCC-PP/MiniC baseline、临时候选 release 和临时候选 sanitizer，`check-ast-contract check-type` 在 release + sanitize 均通过，`MINIC_PARSER_COLD_SWITCH_SANITIZER_CONTRACT=PASS`。额外 9 个独立 .i edge fixture，原版/候选 release/ASan-UBSan **全部三臂行为一致**：普通/重复 switch、128 个 case、15 和 16 层嵌套接受且生成 .s SHA 精确一致；17 层越过历史 16 层限额、重复 case、重复 default、进入 switch 后的错误语法全部按旧行为拒绝；`MINIC_PARSER_COLD_SWITCH_SAFETY=PASS cases=9 arms=3 byte_exact_success=yes`。白盒审查现存 `probe=*parser` 调用点只进行词法扫描、常量语法探针，无 `push_switch_context`；唯一 switch case 写入口在真实 Parser 的 statement switch 解析内，Program 顶层清理处 free 一次。此证据支持冷指针只被借用，避免 131KiB 对所有 lexer-only speculative copies 的重复结构复制；但其覆盖有限，不能当作形式证明所有未来 parser 探针都不能写冷状态。

**生产候选已提交并进入真实运行时门禁：** [commit cbbd5654](https://github.com/yituanxing/minic-toolchain/commit/cbbd5654cc8f7fa508e798bc0262f49e4c32d3ee) 仅合并三处已按现有脚本验证的 C/H 文件（`src/frontend/parser_internal.h`、`parser_statement.c`、`parser_function.c`），保留原 16 层及每层 512 case、各 case 整数语义及 parser OOM/失败诊断；首次真实 switch 懒分配，所有编译结束路径 free。此前 500 A/B **1.08697×**，10-owner 8x triage **0 个 CPU+wall 持续回退**，但合入时**完整 2064/GNU link/QEMU P1 [#39016455728](https://github.com/yituanxing/minic-toolchain/actions/runs/39016455728)** 和 Focused T1 [#39016455740](https://github.com/yituanxing/minic-toolchain/actions/runs/39016455740) **最终都 SUCCESS**，已确认 7/7 shard、`GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`、`COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`、`COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`、`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`。**最终裁决：`parser-cold-switch-heap-owned-v1 = integrated / runtime-certified / keep / do-not-repeat`**；仅证明 GNU PP→MiniC→GNU AS/LD→QEMU 的单 MiniC 替换，不冒称所有 Mini 原生工具均在这一轮同时通过。
**下一热点更新的仪表纠错：** Parser 内嵌冷表移走后，`sizeof(MinicParser)` 已不再 ≥131072B；[#39016651501](https://github.com/yituanxing/minic-toolchain/actions/runs/39016651501) 的 CPU reprofile 初次失败源于旧 profiler `parse_parser_copy_census()` 硬编码 `parser_bytes < 131072` 检查，把已成功缩小的 Parser 误判为不一致，**不是编译器编译错误或 QEMU 运行时回退**。已在 `tools/ci/linux-perf-host-hotspot-profiler-v1.py` 保留全部字节数/站点计数一致性校验、将允许范围改成 256～262144B；新的 post-layout CPU reprofile [#39016792065](https://github.com/yituanxing/minic-toolchain/actions/runs/39016792065) 已单独重跑。在其 CPU 数据完成之前不继续对过期 memcpy 占比抽卡。

## 第六轮候选前置：第五轮之后重新做 CPU 归因（2026-10-17）

**先重新测而非直接改**：截至 `main@fb4ca421d`，上一轮已经将 `MinicExpression=416→208B`，first500 累计编译用时下降 **10.21%**，完整 2064/Linux/QEMU P1 PASS（#39012897095）。此前的 Callgrind 29–46% libc memcpy 指令比例是结构压缩**前**的热点，不能凭过期归因继续搬同一数据。尤其历史 `PERF-033` 改扩容因子否决、`functiontype-exact` <1% 停止、`call-inline8` 已合入，不应被重复抽卡。

**本阶段启动只读 / opt-in CPU profile [minic-cpu-profile-only]**：复用现有 GitHub Actions perf500 job 和 SHA-frozen Kbuild `fs/ext4/fsync.o`、`kernel/bpf/btf.o`，固定相同 runtime-certified GNU preprocessed input；release MiniC 保持未改，独立 `-g -fno-omit-frame-pointer` 诊断工具编译，并运行已存在的 `linux-perf-host-hotspot-profiler-v1.py` 获取真实 Callgrind 函数/指令归因、parser copy census，输出 `.s` identity 门禁，避免只从 208B struct 推测速度。该实验不写生产 C、不替换/回归上一轮的 `src`。

**待真实结果后选卡**：记录当前 memcpy/calloc/realloc、AST normalization、Verifier、Core lowering 与 Specialization 占比；判断下一项能否减少真正热点 `O(N)` 复制/重复扫描，而不是仅因某个结构仍很大就再换指针。历史 Callgrind 和多 TU wall 结果不可相乘或等价比较。

## 空间方向第五轮：CALL 溢出按需存储，消除每个 Expression 的联合体空耗（2026-10-17）

**出发点：** 当前三轮生产优化之后，Expression capacity 仍然占据 [#39011170730](https://github.com/yituanxing/minic-toolchain/actions/runs/39011170730) 七个独立大 TU 共 676 MiB；CALL 参数固定内联 64×32-bit＝256B，即使只有约 6%～7% 的 Expression 是 CALL 也付出该联合体空间成本。已记录历史 `PERF-033` 的数组扩容倍率调整（否决）和 `minic-ast-packed-inline-call-id-v1` 32-bit ID 压缩（**已合入**）均不能重提；本候选是在这个最新 baseline **保留 32-bit ID、不动扩容**、把 CALL inline payload 由 64 slot 缩到 **8 slot**，仅 9～64 参数的稀少 CALL 添加 Program 独占 heap overflow，parser 使用栈 scratch、`program_add_expression` 深拷贝、AST cast normalization 用私有 scratch 进行 remap 后再深拷贝，Program destroy、normalization 成功/回滚分支分别释放正确的 owner。注意这比前几轮更复杂，绝不能仅因省字节提前合入。

**初筛 [#39012074549](https://github.com/yituanxing/minic-toolchain/actions/runs/39012074549) SUCCESS**：`sizeof(MinicExpression)=416→208B`（减少 208B，**缩小 50%**；Function 保持 1416B），`make check-ast-contract check-type` PASS，合成 CALL **0/1/2/8/9/63/64 参数** old/new `.s` byte-identical，65 参数均拒绝。5 个重型 GNU frozen Linux TU 每臂 4 次 paired，**5/5 相同汇编**，old 5344.092ms→candidate 5089.518ms（**1.05002×**，这五个 owner 单独加速约 4.76%），RSS 单次原/新版 KiB：fsync **138048→90724**、super **167692→129652**、BTF **243808→157456**、events/core **230084→184676**、memcontrol **222164→150004**，全部下降；注意这些是单次进程 RSS，并非 Linux 七路峰值总占用。

**first500 门禁 [#39012224627](https://github.com/yituanxing/minic-toolchain/actions/runs/39012224627) SUCCESS**：同 runner 4-worker/250-250 balanced order，500/500 output `.s` **字节完全一致**，baseline **280806.617ms**、candidate **252142.129ms**，**1.11368× 加速**（累计编译耗时下降约 **10.21%**，节约 28.664 秒）；单 TU 倍率中位数 1.11838×。**4/500** 单次 wall 时间看似回退，其中 `fs/nfs/client.o` 719.215→816.381ms，`fs/nfs/blocklayout/extent_tree.o` 872.614→954.098ms，`kernel/irq/manage.o` 612.496→638.081ms，`kernel/sys_ni.o` 12.104→12.144ms。这些不能直接判定为稳定 CPU 回退，已启动六个 owner（四负例+两个正对照）8 old/8 new CPU/user/wall/MAD 测试，并用新增 `tests/frontend/ast_contract_test.c` **64 参数 + normalization + owned overflow 生命周期回归**在 `MODE=sanitize`（ASan/UBSan）下单独验证：[safety CI #39012537673](https://github.com/yituanxing/minic-toolchain/actions/runs/39012537673)。**这是入主线前的历史 first500 阶段；后续安全、正式合入及 full QEMU 已完成，见本节最终裁决**。

**安全与回退复核已通过 [#39012537673](https://github.com/yituanxing/minic-toolchain/actions/runs/39012537673) SUCCESS**：新的 64 参数 CALL 深拷贝/normalize 生命周期用例通过 `MODE=sanitize` ASan/UBSan（`MINIC_AST_CALL_INLINE8_SANITIZE=PASS`），6 个 owner（4 单次慢样本 + BTF/fsync 对照）均在 **8 old/8 new** 交错运行下编译输出完全一致、**6/6 wall 中位数改善、0 个持续 CPU+wall 回退**。典型 `fs/nfs/client.o` 原 first500 单次 719.215→816.381ms 的负值复测后为 **744.947→703.341ms**、user CPU **679.097→640.407ms**；`kernel/irq/manage.o` 441.382→406.906ms。注意 ASan/UBSan 单元验收不能替代完整 Linux QEMU 运行时。

**第五轮现已正式认证并保留：** [生产源码 commit 2861736c](https://github.com/yituanxing/minic-toolchain/commit/2861736c3d577f96de470205f1f38c1f0233bb50) 改动九个真实 C/H 源文件，包括 `src/core/core_lower.c` 的 18 处越界风险访问改为 getter，源清单包括 `ast.h` SHA `97acd676dbaf717710b02632f89451cd97a33e07`。第一次 full Linux [#39012800524](https://github.com/yituanxing/minic-toolchain/actions/runs/39012800524) 因 **源清单中其它源文件 SHA 未同步**被意图中的身份门禁拒绝，而非编译正确性失败；完整刷新原生 compiler/core 全部受影响源码身份后，真正 [完整七路验收 #39012897095](https://github.com/yituanxing/minic-toolchain/actions/runs/39012897095) **SUCCESS**：七路文件分别 295/295/295/295/295/295/294，合计 **2064/2064**，`GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`，`COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`，**`COHORT_P1=PASS syscalls=12 python_era_oracle=PASS`**，最终 `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`。正式版本的 [Focused T1 #39012800514](https://github.com/yituanxing/minic-toolchain/actions/runs/39012800514) 也 SUCCESS。**最终裁决：`ast-call-inline8-owned-overflow-v1 = integrated / runtime-certified / keep / do-not-repeat`。** 此项只证明固定 GNU 预处理/汇编/链接时的 MiniC 编译阶段成功，不能扩展成全 Mini 原生工具链联合验证。此前 500 的 1.11368× 为各 TU 耗时之和，在不同时间、不同环境和基线上测得，与其它轮次不能相乘。

**过程纠错记录：** 初始试验 CI [#39011726643](https://github.com/yituanxing/minic-toolchain/actions/runs/39011726643) 因复用 workflow 字符串替换时写错 patch 脚本名失败；[#39011796905](https://github.com/yituanxing/minic-toolchain/actions/runs/39011796905) 因从 FunctionType 实验复制了错误的 `git diff` 2-file 预期（本候选 9 个 C/H 文件）在编译前被拒；[#39011872732](https://github.com/yituanxing/minic-toolchain/actions/runs/39011872732) 极值测试揭示 Core Lower 对 `value.call.arguments[8]` 仍有直接访问、违反新的 8 inline 边界，现已在 `src/core/core_lower.c` 的暂存改写中使用 getter，随后 `#39012074549` 和 500 测试正常，且 CI 增加 `grep` 确认所有 `src/**/*.c` 均不再直接访问 CALL 参数数组。失败证据保留，禁止把前两条 CI 失败误称算法退化或把第三条真实漏改掩盖成 Runner 噪声。

## 空间方向第四轮选卡：先复测最新源码存储构成，再独立筛选 FunctionType（2026-10-17）

**最新已认证主线白盒重新测量（不是沿用第三轮之前的旧数据）：** [read-only 7 Linux TU CI #39011170730](https://github.com/yituanxing/minic-toolchain/actions/runs/39011170730) **SUCCESS**，`MINIC_AST_SHAPE_CENSUS=PASS`，7/7 `.s` byte-identical。对七次**独立** TU 编译各自数据结构 array **capacity×sizeof（名义存储容量，不等于真实峰值 RSS，也不能当作七路并行累计峰值）**合计为：

| 数据容器 | 七个独立 TU capacity 之和 |
| --- | ---: |
| MinicExpression | **676.00 MiB** |
| MinicFunction（已独立动态参数化后的 header）| **154.84 MiB** |
| MinicStatement | 52.50 MiB |
| MinicFunctionType | 25.23 MiB |
| MinicLocal | 20.00 MiB |
| MinicRecord | 1.23 MiB |

典型大 TU 单次：`btf.o` Expression **104.00 MiB**、Function **22.12 MiB**、FunctionType **4.59 MiB**、Statement **7.50 MiB**、Local **5.00 MiB**。七个 TU 的 FunctionType 平均 signature 参数约 2.28–2.60，但每个 FunctionType 仍预留 64×72B，因此可独立筛选；**Expression 明显仍是剩余空间主导项（676 MiB vs 25 MiB），不能一味追着上轮 Function 改进做微优化**。

**第四轮单因素初筛 [#39011342180](https://github.com/yituanxing/minic-toolchain/actions/runs/39011342180) SUCCESS，但明确不晋级**：`functiontype-exact-parameter-store-v1` 只在 disposable CI 源码生效，真实 `sizeof(MinicFunctionType)=4696→96B`，`sizeof(MinicFunction)` 保持 1416B，5/5 完全相同 `.s`，`check-ast-contract` 和 `check-type` PASS；四对同 Runner A/B 统计五个 hot owner **old median sum 5286.034→5245.047ms，仅 1.00781×（耗时约 -0.775%）**。分样本 fsync 389.793→383.355ms、super 889.439→881.559ms、BTF 1151.153→1139.936ms、events/core 1527.096→1512.821ms、memcontrol 1328.555→1327.377ms；RSS 下降例如 BTF 246868→238864 KiB，memcontrol 227616→220592 KiB。尽管 RSS 节省 3–8MiB 是真实的，但 **即使特意挑选 FunctionType 占 4.59 MiB 的热点大文件，CPU/Wall 回报仍低于 1%**，新增大量小 malloc、修改 FunctionType 所有权会增加代码复杂度。按以前确定的停损规则，**判定 `functiontype-exact = screened-local-pass / low-time-benefit / stop-no-promotion`**：不扩 500、更不跑 2064/QEMU，生产 `src` 未改；保留可复用补丁、CI 证据和本决策，禁止以后把这张已拒卡重新包装成第四轮新优化。转向有量级优势的 Expression（7 TU 676MiB），先处理 CALL 深拷贝/AST normalization/overflow 生命周期问题，再启动安全可回退的独立 A/B。

## 空间方向第三轮：按实际数量单独分配 Function 参数类型（2026-10-17）

**单因素新模型，先只改临时 CI 工作树，正式生产 C 尚未改。** 继前两轮 `MinicType` 72B 和 CALL 参数 ID 32-bit 后，七文件原样追踪 [#39007018275](https://github.com/yituanxing/minic-toolchain/actions/runs/39007018275) 显示 85,327 个 Function 中 77.76% 参数数≤2、均值1.770，然而每个 `MinicFunction` 依然 `MinicType parameter_types[64]`，单靠该数组固定占 4608B、整个 Function 6016B、16,384 容量的重型 TU Function 数组 **94 MiB**。虽原讨论首先考虑“小内联4 + 超额另存”，但存在 **by-value Function snapshots + Function 数组 realloc 后内联指针悬空**，因此先用**更保守的私有 exact-length heap 参数数组**验证真实收益上界（而非冒险直接上 hybrid）；`MinicFunction` 保持原字段直接下标访问的语法，单个 Function 通过 `set_function_signature` 只分配实际参数数 `n*72B`，返回前 deep copy，析构各自 free，源 Function 被克隆到 specialized 时也通过现有 setter deep-copy，常见按值快照仅借用不可释放的参数数组。0 参数不分配，但非零参数每个 Function 添加 malloc，这项**新分配成本必须在真实 CPU/Wall 测试中检验**，不允许将减少 struct 字节当作真实收益。

**五个 SHA 固定 Linux owner A/B [#39008785116](https://github.com/yituanxing/minic-toolchain/actions/runs/39008785116) SUCCESS**：同 runner，每臂 4 次交错，`make check-ast-contract` PASS；旧/新实际 `sizeof(MinicFunction)=6016→1416B`，字段宽度 `sizeof(MinicType)=72B` 不变，函数类型描述符未改，Specialization fixed fact arrays 未动；Linux 输出 `.s` **5/5 byte-identical**。代表性每 owner 的 old P50 → new P50：
- fsync **338.417→305.659ms (1.10717×)**；峰值 RSS 单进程 **182372→137688 KiB**；
- BTF **992.052→903.015ms (1.09860×)**；RSS **306592→243728 KiB**；
- events/core **1359.769→1200.996ms (1.13220×)**；RSS **289216→231772 KiB**；
- memcontrol **1192.284→1060.027ms (1.12477×)**；RSS **285756→227712 KiB**；
- ctype 2.670→2.521ms，短命毫秒级噪声意义低。
**五 owner 中位编译时间相加 3885.191→3472.217ms，1.11894×（累计用时减少10.63%）**。这些是特定 hot owner，不是任意 500 TU 或七路 Kbuild speedup；一次进程 RSS 不能直接称全 Linux 内存下降。

**扩大 frozen first500 [#39008901495](https://github.com/yituanxing/minic-toolchain/actions/runs/39008901495) SUCCESS**：沿用 existing CI same-runner 4-worker、250/250 balanced order，SHA 冻结 500 个 GNU Kbuild `.i`，**500/500 byte-identical `.s`**；baseline **336058.147ms**、candidate **316391.869ms**、**1.06216× 提速，相同 500 编译工作累计用时减少 5.852%（19.666 秒）**；单 TU 倍率中位数 **1.06065×**，仅 **20/500** 单次 wall 回退；这是本轮不同时间段的新基线，不能与其它优化的加速比直接相乘。近 10000 Function/TU 额外 malloc 后依然取得 500 编译总用时改进，证明“所有额外 malloc 必然导致慢”并不符合这些输入的实际观测，但尚需 full kernel link+QEMU 安全认证。

**回退细查 [#39009160280](https://github.com/yituanxing/minic-toolchain/actions/runs/39009160280) SUCCESS**：first500 20 个单次回退中选择绝对退化最大的 8 个，另加 BTF/fsync 两个正对照，各自 old/new **8 次交错**，真实 `wall/user/sys/MAD` 与 10 份完整 JSON 留档。**10/10 `.s` byte identical，10/10 wall P50 改善，0/10 同时复现 wall+CPU 双重回退**。最明显原单次 `fs/nfs/client.o` 809.573→947.069ms，重测 643.618→587.184ms、user CPU 581.504→531.924ms；`kernel/cgroup/legacy_freezer.o` 289.846→265.109ms；`kernel/events/callchain.o` 300.088→273.363ms。剩余 12 个单次负值没有逐个 8× 复测，原始清单完整保存。此验证对首轮噪声有反证，但不等于任何潜在极端内存压力和 OOM 路径完全消失。

**源码晋级（需独立运行时认证）：** [Production commit d244df7d](https://github.com/yituanxing/minic-toolchain/commit/d244df7df25dda8ef282280f6540bef9a40eff29) 已将原 opt-in 单因素 **2 个生产 C/H 文件**落地，`sizeof(MinicFunction) = 6016→1416B`，0 参数空指针无 allocation，非零参数各有一份 owned exact-length copy；保留 64 参数上限及全部 Specialization fixed fact 数组，签名setter分配失败时不改变已有 Function，Program destroy释放，按值快照只借用稳定堆存储，不按值复制释放 ownership；之前的 `MinicType` 布局与 CALL-ID 优化不受影响。Native source-header pinned SHA 已同步到 `88875de9638d93fcd0fed37f9853085a82943ba5`；这仅确保源代码身份，**不代表** QEMU 验收通过。新的 [Focused T1 #39009342065](https://github.com/yituanxing/minic-toolchain/actions/runs/39009342065) **SUCCESS**，完整七路 [2064 Linux + GNU Link + QEMU P1 #39009342014](https://github.com/yituanxing/minic-toolchain/actions/runs/39009342014) 也已最终 **SUCCESS**：7/7 完整 C 分片、`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`，无需重编的 GNU 链接 `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`，真实 QEMU `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`。**最终裁决：`minic-function-parameter-exact-owned-v1 = integrated / runtime-certified / keep`**，新的 `MinicFunctionType` 数据布局或更进一步的 per-Function arena/inline4 均属于未测试的不同候选，不能宣称其收益已包含在此版。仅证明 GNU PP→MiniC→GNU AS/LD→QEMU 单编译器替换的完整 Linux Runtime；全 Mini 自制工具联合构建并非此次验收范围。

**额外生命周期定向单测 [#39009503446](https://github.com/yituanxing/minic-toolchain/actions/runs/39009503446) SUCCESS**：向现有 `tests/frontend/ast_contract_test.c` 增加独立可复用的 Program Function 参数所有权测试，覆盖：原 Function 指针在 >100 次 Function 追加和头部 realloc 后依旧稳定、按值 snapshot 借用、克隆函数对参数数组深拷贝、source 签名重设后 clone 不受影响、自身数组作为 setter 参数时不先释放造成 UAF、无效 void 参数时先前合法签名保持不变、64 参数极限、0 参数释放指针；复用 existing perf500 job `make check-ast-contract`，`MINIC_FUNCTION_PARAM_OWNERSHIP_CONTRACT=PASS`，**未增加 workflow/job**。这是生产源码与测试源码不同提交，仅当基于同一已合入源 HEAD 时测试成立。

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

