# Mini Toolchain 性能优化：从慢文件到可验证的通用算法改进（V1）

更新时间：2026-10-14。研究对象：Linux 6.6.143 RV64，MiniPP / MiniC / MiniAS / MiniLD。此文档是可持续补充的**工程实验记录与操作手册**，不是优化完成声明。

## 1. 目的与可重复的证据链

**慢文件 → 结构特征 → 编译器处理路径 → 复杂度假设 → 最小变更 → 配对 A/B → 正确性 → 小范围推广 → 原生 Linux P1 复验。**

以下内容即使实验失败或假设被推翻也要保留，因为它可以减少下一次重复探索、无意义 CI 和重复构建。

每条记录须区分四种证据等级：
- **观察（OBSERVED）**：直接来自已固定身份的文件、GitHub Job 日志、构建产物或代码；注明如何采集。
- **假设（HYPOTHESIS）**：与观察吻合，但尚未证实为主要耗时因素；不得写为已定位根因。
- **实测（MEASURED）**：有相同输入、硬件、工具身份与可复现的基准/候选 A/B 时间。
- **已验证（CERTIFIED）**：对应要求的编译、汇编、ABI/ELF、链接和 QEMU 运行时门禁真实通过；仅限对应提交和配置。

## 2. 通用单案例记录模板（每次优化或失败均填写）

| 字段 | 最低要求 |
| --- | --- |
| Case ID / 日期 / Owner | 稳定编号，例如 PERF-001；改动文件、负责人或分支 |
| 现象 | 哪些对象慢：具体路径、阶段、毫秒数、P95/P99、来源 Run/Job |
| 对照文件 | **预处理后大小接近但快**的一个以上样本；说明控制变量 |
| 输入身份 | Linux 版本、.config SHA256、源文件/.i SHA256、Kbuild flags、ABI |
| 工具身份 | 旧/新 MiniC、MiniPP 等执行文件 SHA256；构建选项、Runner 型号 |
| 结构计数 | Token/宏展开次数/AST 节点/函数/声明/基本块/IR 值/LOAD-STORE 等，记录采集方式 |
| 处理逻辑 | 涉及的编译器函数/源码行；能否给出调用频率或迭代次数计数 |
| 复杂度 | 原算法随何种量增长，预测哪个类型文件被放大；给出上界假设 |
| 改进方案 | 索引、缓存、单次遍历、缩小工作集或算法更换；附失效/回退语义 |
| A/B | 只改一个变量；逐文件 before/after、三次以上重复、聚合、最长尾及波动 |
| 正确性 | 未改语义则逐字节汇编 SHA；语义有变则 ABI/ELF + 功能语义 + Linux QEMU P1 |
| 内存 / 空间 | Max RSS、.i/.s/.o 体积（顺带测量，不能掩盖时间退化） |
| 结论与状态 | rejected / needs-data / verified-local / verified-500 / runtime-certified；明确仍缺什么 |
| 复用 | 旧缓存、测试程序、已有 Runner/Job、产物 ID、下一次复跑命令及关联提交 |

**特别要求：** 已经成功的旧 Image 和对象缓存保持不可变。新优化改变编译器可执行文件的 SHA，就不能拿旧 Image 的 P1 作为新优化的运行时认证。

## 3. 时间数据：不要混淆不同概念

| 指标 | 定义 | 适用 |
| --- | --- | --- |
| stage wall ms | 逐进程 monotonic/perf_counter 计时（单个 TU 的 MiniPP/MiniC/MiniAS） | 归因瓶颈和逐文件排行 |
| sum of TU wall | 各文件耗时求和（可横向比较工作量） | 找高频低效算法 |
| shard critical path | 七路里最慢的真实 wall time | 预测完整构建能缩短多少 |
| end-to-end CI wall | 环境、缓存恢复、链接、QEMU、上传均计入 | 用户实际等待时间 |
| cold/warm | 缓存冷/热分别记录，配对顺序交替并在同一 Runner 执行 | 避免把文件系统或缓存差异误判为编译器优化 |

跨分支、不同 .config、不同 2057/2064 个对象、不同工具或不同 GPU/CPU 机器的数字，**不允许直接算成优化前后速度提升**。先固定输入和工具身份，再做配对实验。

## 4. 初步候选案例（所有状态截至 2026-10-14）

### PERF-001：Parser 名称查找与布尔特化联合作用

**OBSERVED / MEASURED（联合实验）**
- Linux first500：同一批 SHA256 冻结的 500 个 .i 输入，四工作线程，原先现成的 paired A/B 跑了两份性能候选。
- 基准配置：关闭 typedef 索引和局部布尔特化；候选：同时开启上述两项。
- 500 个 MiniC -S 子进程耗时之和：**1638.145 秒 → 595.860 秒（2.7492×）**。
- P95：10.397 → 2.244 秒；最慢 TU：21.159 → 4.973 秒。**500/500 输出汇编 SHA 相同**。
- 原始 CI：<https://github.com/yituanxing/minic-toolchain/actions/runs/38571430622>，证据文件为 linux-current-profile-first500-perf-v1 artifact。
- 候选索引计数：**9,212,810 typedef 查询、14,690,549 探测、0 fallback、3473 次 rebuild**。
- 该测试不是生产 Runtime 39 补丁编译器，也不代表全 2057 RV64 C 对象加速 2.75×。

**HYPOTHESIS（不能先写成结论）**
- src/frontend/parser_core.c 与 src/frontend/parser_typedef.c 中旧名字查找存在反向扫描路径；许多声明和反复引用会放大查找总迭代次数。
- 旧路径查找开销最坏接近 O(Q × N)，Q 是查询次数、N 是扫描候选数。typedef 哈希索引可能降低这部分，条件布尔专门化也可能显著减少其他处理；**现在不知道各贡献占比**。
- 静态源码结构（声明数、函数数、条件表达式数）只能提供预测；必须结合查找计数、具体函数计时与独立开关交叉检验。

**已完成的最小方案与验证**
- 复用已有 typedef hash 与布尔特化补丁，默认 Runtime 构建未变。
- 复用原来的 perf500 workflow、输入 SHA 校验、同 Runner 四工作线程 A/B、汇编相同检查与 ASan；没有另造性能测试框架。
- 新增 **只在 opt-in 时生效**的 MINIC_PERF_SKIP_TYPEDEF_INDEX、MINIC_PERF_SKIP_BOOLEAN_SCOPED 开关；复用同一个 linux-perf-typedef-paired-ab-v1.py 进行两项单因素 A/B。
- M0 的 archived-job 校验、canonical workflow 清单和 Legacy Router 引用校验更新并通过（M0 #38575625827）。
- 因素拆分验证 run：<https://github.com/yituanxing/minic-toolchain/actions/runs/38575183573>。**目前不把该实验视作已完成或已有单因素数据。**

**下一步**：记录 (neither / typedef-only / boolean-only / both) 各自的耗时与汇编 SHA，计算单因素及交互收益；有独立收益才考虑推广到 RV64 慢文件并做 P1。

### PERF-002：小文件编译极端长尾——blake2s

**OBSERVED（历史已定位问题）**
- lib/crypto/blake2s-generic.i 仅 **148390 bytes**，历史某次 MiniC -S **1,981,990ms**（33 分 02 秒），成为七路关键路径；真实记录见 <https://github.com/yituanxing/minic-toolchain/actions/runs/38400728544>。
- 同目录下已有完整定位文档：docs/ci/linux-blake2s-cfg-slot-lookup-performance-2026-10-13.md。

**已找到的真实编译器路径**
- 由 tools/ci/apply-riscv64-p1-defined-before-use-crossblock-v1.py 修改的 src/target/riscv64/core_codegen.c 中，原先对每个对象循环扫描块，遇到每个 LOAD/STORE 再搜索 OBJECT_ADDRESS 生产者。
- 原先高阶上界约 **O(objects × memory-ops × block-length)**；方案是按稠密 Core Value ID 建生产者位置索引、分为两次遍历，使该检查对每个对象的工作接近 O(value-count + block-length)，并在内存失败时保守回退。
- 这是已追踪到源码的历史复杂度问题。**不要把与另一个日期、另一个配置测得的约 2.71 秒直接解释成同一基准优化了 731×**。

**下一步**：对同一冻结 .i，同配置旧实现/新实现逐段打点比较。保留原先的栈帧、CFG 合法性和写前读判定，单 TU 结果通过后复用七路 P1 才能认证。

### PERF-003：大网络文件结构与编译时长关联

**OBSERVED / HYPOTHESIS**
- 2057 个 RV64 C 对象阶段数据（七路 CI #38566408145）显示 MiniC 累计耗时占核心 PP/C/AS 三段绝大部分，网络文件反复成为慢文件。
- 从 Linux 6.6.143 **原始 .c** 粗略词法统计：net/core/filter.c 中约 110 switch、807 case、793 if；net/core/dev.c 中约 366 个函数定义、903 if、131 goto。仅是源文件文本启发式统计，**不等于预处理后的有效 AST/IR 个数**。
- 可证伪假设：复杂分支 CFG、巨量函数/声明查找、复合表达式与展开后的内联函数可能使某些 MiniC Pass 重复走访工作集，导致相近 .i 大小但耗时不同。

**对照与具体行动**
1. 同一 Linux 配置下选慢文件 net/core/filter.i、net/ipv4/udp.i 与大小相近但较快的 net/ipv4/tcp_cubic.i；记录 frozen .i SHA。
2. 复用 MINIC_BOOTSTRAP_TRACE 的 parse/normalize/core-set/core-validate/layout/codegen 边界；如计时精度不足，只增加受环境变量控制的单调时钟埋点，不改变热路径默认行为。
3. 计数每 TU 的标识符查询及 probe、AST 函数/声明、Core 函数/基本块/值/LOAD-STORE、switch/case，按每阶段耗时做简单比较。
4. 定位具体高阶重复扫描后先改一个算法，旧版与新版在**同一文件、相同 runner**单变量 A/B；验证输出和运行时，再推广至 500 或 2057 TU。
5. 如果分析发现瓶颈来自别的阶段，就将该假设记为 rejected，保留负例。

### PERF-004：MiniPP、MiniAS 和 MiniLD 同样按此流程记录

- **MiniPP**：比较同一原始 C 和相同 GCC predefined flags 上的 GNU-E / MiniPP-E 时间；计数 Token、对象式/函数式宏展开、嵌套层数、include 命中和重分配，验证预处理输出等价性。只对真实热点增加低侵入计数。
- **MiniAS**：固定相同 .s，计指令数量、伪指令展开、符号/重定位查找、字符串表容量增长、输出对象字节；绝不能用“联合 C 编译 18 分钟”代替 MiniAS 的纯耗时。
- **MiniLD**：固定同一组不可变 .o 与链接脚本、配置、工具 SHA；区分最终链接、-r、vDSO/purgatory、归档；计符号/重定位数和每次真实调用的耗时、RSS。历史本机 310798ms + 306902ms 仅为某个启用了原生工具组合的两次链接，须用 minild-times.tsv 区分究竟是谁花的时间。
- 每个阶段记录 **关键路径收益**和 **sum TU wall 收益**；优先让 CI 的反馈循环缩短，再顺路观察内存峰值与 .s/.o/Image 体积。

## 5. CI 复用和升级门槛（禁止“假绿”）

1. **先读已保存的七路/500 TU/2057 TU artifacts、Job 日志和经过 SHA 校验的 frozen inputs**，不重复生成前面的 .i/.o。
2. 围绕少量慢+同大小快的文件做 A/B：同配置、同 Runner、交替顺序、相同 flags，逐文件对比；报告 P50/P95/P99/最大值以及绝对毫秒。
3. 仅当单文件有清晰收益、无退化且输出语义/字节验收通过，才放大到已有 first500 或从全量 2057 选择的精确 owner 集合。
4. 涉及生成对象的优化必须以新的 MiniC/MiniAS 工具 SHA 重建所影响对象；完整 Linux link + QEMU P1 才是可宣称的 Runtime 成功。
5. 保留失败测试、错误日志、没有变化的控制组；M0 结构通过**不是**编译正确，500/500 输出一致**不是**Linux 运行时通过，最终 CI SUCCESS 也需查用户要求的实际模式是否真正执行。
6. 现成脚本优先：tools/ci/linux-perf-typedef-paired-ab-v1.py（同输入 A/B），tools/ci/linux-perf-build-minic-profile-v1.sh（隔离工具构建），tools/ci/linux-runtime-gcc-minic-ab-v1.py（单 TU 锁定 GNU 参考），现有七路 cache+QEMU workflow（最终晋级门槛）。避免新建重复 workflow/job。

## 6. 如何追加记录

后续每轮只追加 **现象 / 证据 / 判断 / 改动 / A/B / 正确性 / 成本 / 结论 / 下一步**，附确切 CI Run、Job、artifact ID、工具哈希和输入哈希。失败也要写原因以及如何避免重测。不同分支的已有数据要保留参考身份，不在报告里把“已推测”“已编译”“已 QEMU 认证”混写。

## 7. PERF-005：七路编译关键路径不均衡（已经有实际测量和可运行的离线调度器）

**OBSERVED（来自成功的 RV64-only 2057 C 对象实验）**
- run #38566408145 的每个对象有独立的 `gnu_pp_wall_ms`、`minipp_wall_ms`、`minic_wall_ms`、`minias_wall_ms`，并带有旧 shard ID。
- 在原先按 294/294/294/294/294/294/293 个对象均匀分配的情况下，**联合管线各 shard 累计实际进程 wall 之和**（毫秒）：[1,067,993; 909,986; 930,321; **1,097,098**; 643,202; 1,040,565; 1,076,370]。同一个 2057 对象集合的 MiniC 独立各 shard：[821031,762332,728229,**842622**,502034,796858,833089]。
- 因此按“文件数量”均衡，并不能保证按“编译时间”均衡。即使 MiniC 算法暂时不变，仅重新排布耗时较长的对象，也可能降低最慢 Runner 的等待时间。

**HYPOTHESIS / COMPLEXITY**
- 将 2057 个已经测时的 owner 作为带权任务。仅针对七路 Runner 的固定容量上限（每路最多 294 个），按耗时从大到小排序，优先放入当前累计负载最低且有空位的 Runner（LPT + count capacity）。算法开销 O(N log N + 7N)，远小于任何实际编译。
- 时间的权重来自同一配置的**历史单文件实测**；不是静态用 .i 大小猜测。新工具必须检测输入哈希、owner 集合、负权值或重复文件，失败立即停止。

**OFFLINE REPLAY（非 Linux CI 加速成绩）**
- 使用历史逐文件耗时重放，老方案最慢联合进程总耗时 **1,097,098ms**，LPT 预计最慢 **966,508ms**，理论潜力 **-11.90%**。
- 单独 MiniC 最慢从 **842,622ms** 预计降到 **755,171ms**，潜力 **-10.38%**。
- 这只是独立阶段 wall 的分配仿真；并非重新运行后的 CI 实测。缓存、CPU 抖动、链接、下载、并行内存竞争都会影响实际关键路径，**禁止说“已经快了 12%”**。
- 已添加 `tools/ci/linux-perf-time-balanced-shard-plan-v1.py`，它只产生计划 JSON，不触碰已有 2057/2064 个对象、CI 分片生产者、归档和 SHA 身份。自测复用现有 M0 Source Gate #38581032252：**SUCCESS**，无需任何七路重编。

**下一次晋级条件**
1. 使用同样的 frozen .i 清单及同一 RV64 .config，按计划重新分配一个**真正需要重新编译**的新 Compiler SHA 作业（不因只测试调度而重编老对象）。
2. 生产者和合并器同时更新版本化的 shard owner 清单与 checksum，必须验证每个原始 owner 恰好出现一次且 ELF/ABI 正确，旧缓存绝不冒充新 shard。
3. 对相同优化候选比较“原数量均分 vs 带权计划”的最长 Runner 墙钟时间；分析整个 CI link+QEMU 时间是否真的受益。

此项属于**验证反馈循环的调度优化**，和针对 MiniC 算法复杂度的 PERF-001/002/003 分开记账，避免掩盖真正的编译器算法优化需求。

## 8. PERF-001 真实实验补充：单因素结果与一次超时失败

### 已完成的真实单因素证据（2026-10-14）

第一次四臂扩展 run #38575183573 原始第一对照（neither vs both）再次 **500/500 汇编 SHA 完全一致**，其运行内累计 MiniC -S 耗时 2940.789→1075.635s，实际该 Runner 环境中的倍数 **2.7340×**。同一 run 的第二步（Boolean-only vs both）也 **500/500 输出一致**，累计 **1087.203→1057.736s，both 相对 Boolean-only 仅快 1.0279×**。该对照说明在这一批文件上**新增 typedef 索引的边际收益只有约 2.8%**，布尔专门化可能解释联合加速的大部分；但独立布尔效应仍需补做完整对照才能下结论。

**失败也必须记录：** run #38575183573 在第二组（Typedef-only vs both）进行到 375/500 时因作业时间达到约 40min 而 `cancelled`，并未产生合法的完整第二组 A/B 结论。上传的 artifact `linux-current-profile-first500-perf-v1`（ID 11903960220）保留已成功的第一组 500/500 因素结果和两组编译器配置 SHA。**不允许把该 run 当作整体绿色，或假装剩余125文件已完成。**

### 为降低验证成本所作方法修正

- 原设计重复运行原始 500 对，再跑 **2×500** 个新的对照，虽然复用了输入缓存，却没有复用结论本身；这不符合我们“只付必要成本”的方法论。
- 保留原 500/500 正确性与性能比较不变；用这批 **真实且完整** 的 paired-results.json，确定性选取 48 个代表（旧版最慢16、绝对节省最多16、中央8、最快8、重叠时按旧版耗时补齐）。当日离线选择回放通过：48 个唯一 owner，最慢16个全部保留。
- 直接扩展**原** `tools/ci/linux-perf-typedef-paired-ab-v1.py`：默认仍必须比较 500 个；只有显式 `--hotspots-from <certified-500-results> --sample-count 48` 才运行小样本，两项 A/B 都要求 **48/48 汇编 SHA 一致**、固定4线程、同一 frozen 500 .i 清单且输入哈希验证。
- 因素筛选这部分成本从重复 **2×500** 次成对编译降为 **2×48** 次成对编译，处理的**样本对数量**减少 90.4%；这不是主编译器的实际速度提升，也不是完整 500 的因素认证。随后若发现某因素确实影响多个慢文件，再单因素扩展到 full500 和 RV64 全量/P1。
- 保留前述 cancel artifact；不为补齐统计而再次触发两轮完整500因素测试，不重编已经合格的2057原生对象。
- GitHub M0 #38582069985 **SUCCESS**，原工作流主体仍由归档哈希检查保证不变；优化条件仅在旧 perf500 Job 末尾显式 opt-in。

后续 Run 必须逐项核对：初始500配对 PASS，48样本组成/哈希身份，48/48 factors PASS，Artifact 下载可重分析。比较已有 full500 条件结果时必须注明它与热点样本的覆盖差异。

## 9. PERF-001 单因素实测结果：Boolean Domain 才是当前主因（2026-10-14）

**正式数据，非预测：** <https://github.com/yituanxing/minic-toolchain/actions/runs/38582346036> 完整 `SUCCESS`，CI 产物 ID **11904408725**，文件 `factor-boolean-only-results.json`、`factor-typedef-only-results.json`、`paired-results.json` 可复查，并已下载归档。相同缓存 first500 .i，四个 Runner worker，不新建工作流，每组 A/B 全部要求 .s SHA 一致。

| 控制实验 | 基准耗时（sum TU wall） | 候选耗时 | 加速倍数 | 正确性 |
|---|---:|---:|---:|---|
| 500：两项都关闭 → 两项都打开 | 1443.429s | 503.635s | **2.8660×** | 500/500 汇编 SHA 相同 |
| 48：仅 Boolean → 两项都打开 | 75.322s | 74.637s | **1.0092×** | 48/48 |
| 48：仅 Typedef → 两项都打开 | 413.340s | 75.620s | **5.4660×** | 48/48 |

因果解释：在这批特意包含长尾慢文件的 48-TU 固定样本里，**开启 Boolean Domain 缓存**才让只有 Typedef 的候选大幅提速；在 Boolean 已开启时额外加入 Typedef 哈希索引仅提高约 0.9%。这仍不代表 *整个 Linux* 有 5.466× 加速，也没有证明 Typedef 索引在其他输入或与 Boolean 之外的组合中没有价值。

代表性慢例 `kernel/bpf/btf.o`：原始两项都关闭约 19.432 秒；Typedef-only 与 Both 的巨大差异再次说明优化前多次重算 Boolean Domain 的成本在该样本上占主要比例。现有 Boolean Index patch 是 **按 Program + AST Arena 规模认证的缓存**，避免每次局部查询扫描所有相关 Statement/Expression/InlineAsm；分配失败走原线性算法，清理防止不同 TU 的地址复用产生误命中。主要省时机理需以独立阶段 CPU/调用计数进一步证实，不能从倍数直接推出实际 O(n²) 已严格出现。

### 向真实 Runtime MiniC 的最小晋级

- 不修改已认证的生产 Runtime 编译器配置或 2057/2064 产物。
- 在原有 `linux-runtime-gcc-baseline.yml` 的 **gcc-single** 一对象分支（blake2s）里，仅当同时声明 `[linux-runtime-blake2s-only]` 与 `[linux-perf-boolean-single]`，保存原候选 MiniC 执行文件，然后应用已通过 perf500 的 `apply-inline-specialization-local-boolean-domain-scoped-v1.py`，原位增量 make 重建 MiniC，并把对应 patch 文件 digest 添加到对象缓存身份。
- 这条实验仅生成 **一个真实 Linux 6.6.143 C owner**，核验真实 GNU ABI 与原生候选对象的流程能否通过；不运行全部七路、链接或 QEMU。此阶段即使 `SUCCESS` 也只能声称 **单 TU 验证**。
- 单 TU 通过且工具差异明显后，才允许用现成七路生产者重编新 Compiler SHA 对应对象，并在 QEMU 取得新的 12/12。绝不拿旧 Image 的 PASS 当成新优化的 Runtime 证明。
