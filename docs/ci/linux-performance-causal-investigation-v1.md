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

## 10. PERF-006：真实 Linux blake2s 的 Boolean Domain 单文件晋级实测（2026-10-14）

**最终通过：** GitHub Actions run #38587622296（gcc-single SUCCESS），Job #115933721955；artifact #11906495606；commit c759c005749e3aab18905c3f0b0cc65c0d241c07。该运行没有执行 QEMU P1，也没有编译 2057/2064 全量 Linux 对象。

- 固定输入是 Linux 6.6.143 真正 GNU Kbuild 生成的 `lib/crypto/blake2s-generic.minic-stage2.i`，**148390 bytes**，SHA256 `4d9a426cc72da3c9ea579fa94dc8bf8f4622631cc34c081ea066758cc4a2681f`。
- 原生产 Runtime + crossblock MiniC-cc SHA256 前缀 `bfe15a4ec64d51bf`；开启仅一个布尔域缓存补丁之后的 MiniC-cc SHA256 前缀 `8cd8dcda73e096e8`。CI 强制两个编译器真正不同，并在对象缓存身份中同时绑定真实 `minic-cc` 和 driver，而非仅比较没有变的 `minic` driver。
- 冻结原生 `.i`，同一 runner 分别热身一次，并执行每臂 4 次、交替先后次序的真实 `-S` 测量。**基准 P50 1465.171 ms → 布尔缓存 P50 1368.341 ms = 1.0708×；耗时下降约 6.61%**。
- **8/8** 配对输出汇编 SHA 相同（热身亦比较），与原有单 TU `COHORT_ABI=PASS objects=1`、`COHORT_GNU_PREFLIGHT=PASS objects=1` 同时成立。历史候选缓存命中时只重编该一个对象来重建冻结输入，并要求对象字节与之前缓存对象 **完全一致**。零其他 C 对象重编。
- 此处 blake2s 的 1.0708× 是真正匹配输入与两版 Runtime 的局部结果；first500 48 长尾子集联合配置的 5.466× 是其他工具/输入的不同实验，**绝不可混为整体速度提升**。Blake2s 长尾尚可能来自其他 Pass，不能据此宣称 Boolean Cache 是该文件的根本瓶颈。

**测试复用过程中暴露并修复的设施问题：**
1. Kbuild GNU 预检会重新生成 `init/version.o` 与 `vmlinux.o`；只在一文件 + pinned config + exact replay scope 下排除二者的非稳定哈希，其余 GNU 参考对象始终校验。
2. `minic` 实为 driver，可执行编译器逻辑在 `minic-cc`；修正错误的身份判定，严格验证真实编译器 SHA 不同。
3. 单对象候选缓存命中后无法直接拿到先前的临时 `.i`；复用已有 Kbuild 只重编该对象，要求新旧对象字节完全一致，并在复原 GNU 快照前保存真实 `.i`。
4. A/B 按输出汇编 SHA 和多个重复时序判定，不因整体 CI 绿灯就声称完整 Linux P1 已通过。

**结论：** PERF-006 状态 `verified-local`；局部正确性已经证明，性能收益有限。后续应优先测量对 Boolean Domain 更敏感的 BPF/mm 长尾文件，再决定是否进行新的 2057 对象全量生产和 QEMU。再次复用 `tools/ci/linux-perf-pinned-single-paired-ab-v1.py` 和缓存的精确 .i，避免只为性能猜测重编完整内核。

## 11. 性能优化优先级：最长尾轮转与停止规则（2026-10-14）

这是工程决策规则，而不是对任何单一慢文件追求完美。

1. **热点选择**：从相同 Linux 配置、相同工具 SHA、相同指标的逐文件计时中，按对全量关键路径的贡献度及可复现性排序。首先看当前最慢的文件与多个同类文件，而非坚持处理历史上曾经最慢的文件。
2. **先问收益**：每轮可修改一个通用算法，但必须安排一个慢文件和一个对照文件；若仅对一个旧热点节省几十毫秒而其他高耗时 owner 仍然很慢，应转移目标，而非继续打磨该文件。
3. **优先级门槛（工程筛选启发式，非数学证明）**：以相同 Runner 的 4 次交替配对计时为准。热点组中至少一个未被优化过的慢文件取得可重复的两位数百分比改善，或多个热点合计有明确绝对秒数收益，才值得推广该通用算法；否则记为局部收益、停止继续细抠，转向下一类结构或工具阶段。CI/ELF/ABI/汇编一致性检查不过，不能用性能收益抵消。
4. **控制反馈成本**：复用已有 gcc-single 与 frozen GNU source/objects，不启动 2057 C 全量；只有小样本在真实 Runtime 工具上获得足够收益才晋级已有七路与 QEMU。
5. **每轮最多 1 个主要复杂度假设 + 一个对照**：无法从调用计数或阶段计时支持假设，记录为不确定并移至其他高热点；不允许无限修小细节。

### 本轮转移：结束 blake2s 的持续微调，转向当前高工作量对象

PERF-006 `blake2s` 的真实 Runtime 布尔域索引收益为 1465.171ms→1368.341ms（仅 **1.0708×**）；其历史曾有 33min 病理值但那一高阶重复扫描已经解决，因此当前不再优先细抠该文件。

下一组专门选择三种具有显著真实历史编译耗时的 Linux 6.6.143 对象：

- `kernel/bpf/verifier.o`：大型 BPF 验证器，具有复杂分支和数据流；
- `kernel/bpf/btf.o`：类型信息密集，先前 first500 配对实验慢；
- `net/core/filter.o`：网络路径复杂且反复进入慢文件名单。

实验只比较**原已认证 Runtime+crossblock MiniC-cc 与仅开启 Boolean Domain 索引的 MiniC-cc**，用同一个真实 Kbuild 生成的 .i 与同 Runner 四次交替测量，逐文件强制生成汇编 SHA 相同和 RV64 .o ABI 正确。比较三者绝对节省秒数与比例，**禁止把首次单文件成功声称为完整内核的提速**。

实现复用原有 `.github/workflows/linux-runtime-gcc-baseline.yml` 的 gcc-single Job 和 `tools/ci/linux-runtime-gcc-cohort-bisect-v1.sh`；只允许明确的 3 个 owner、固定 GNU 配置 SHA、相同源码与严格 GNU 预检，生成 .i 时对选定对象缓存命中才做一次字节相同的对象回放。冻结 .i 后调用既有 `linux-perf-pinned-single-paired-ab-v1.py`；既不创建 workflow，也不重复构建全部对象。待 CI 出现真正 PASS 后追加详细计时与原因，否则保留失败并先判断是否仍值得优化。

## 12. PERF-007：对三个真正慢文件完成同条件运行时编译器配对测量（2026-10-14）

**验收：** GitHub Actions run #38593657933，gcc-single 完整 `SUCCESS`；原生 Linux 6.6.143 GNU Kbuild .i，全程同一 Runner，固定原 Runtime + crossblock 对照，仅打开已经证明的 Boolean Domain 缓存，双方各 4 次交替测量并热身；每个 TU 的生成汇编 SHA 一致，3/3 ELF/ABI PASS，GNU 全部原始 .o SHA 校验通过。**未运行 Linux QEMU**，所以状态是 `verified-local-three`。

| 原生对象 | 旧编译器 P50 | Boolean 缓存 P50 | 绝对收益 | 加速比 | 输入 .i SHA256 |
| --- | ---: | ---: | ---: | ---: | --- |
| kernel/bpf/btf.o | 15,077.178ms | 7,772.015ms | 7,305.163ms | **1.9399×** | `2e89f8c15c2d5eee7fdb77afda2f6ee28d958f117f9d3d2cc9b738624e298b9a` |
| kernel/bpf/verifier.o | 16,169.658ms | 8,925.430ms | 7,244.228ms | **1.8116×** | `c0b3c94bbc616bf6f1a52427085cd2c30dc1fe5b3c863ee70267ae1fd0beac9e` |
| net/core/filter.o | 32,227.778ms | 18,021.032ms | 14,206.746ms | **1.7883×** | `a05401981af43328acad0a354bb354aa1e0c0231e3339f94505e2dc34c802778` |

这三个样本的**单次各编译一次**之中位耗时合计从约 **63.475 秒降至 34.718 秒**，绝对可节约 **28.756 秒**。注意此结果不是完整 2057 C 对象的总加速；其他文件的收益和退化仍需测量。

**对方法论的影响：** 已经在三种不同的真实热点结构上得到约 1.79–1.94×，足以证明该优化不只是适合 blake2s 的微小收益；无需继续重复研究同一布尔索引算法。下一轮优先研究尚未消除的 Core IR、Codegen 及网络规模带来的处理成本。Boolean Domain 补丁应纳入候选晋级，但不能未经新的全量正确性/QEMU 认证就覆盖已通过的 Runtime 标准配置。

**实际测试设施复用及修复：** 原首轮 #38592358943 已真正通过 GNU 预检、3 对象 MiniC/ABI，但因 `tee` 的报告父目录缺失而失败。随后把昂贵的 A/B 与候选编译拆为不同 Workflow Steps：候选 3 个 .o 缓存独立保存，确切 3 份冻结 .i 及 SHA256 Manifest 也独立缓存；因此本轮复验实际日志确认 `COHORT_CANDIDATE_CACHE=HIT objects=3`，不需要重编三个候选对象。它只在可信固定 owner、Kbuild 源和候选身份匹配时允许复用；没有新建 workflow 或 job。

### 下一组热点：不能继续只测同一种优化

从独立 RV64-only 2057 TU 全量逐阶段真实数据汇总：MiniC 累计 5,286.195 秒，其中 `net/` 目录 **1,807.863 秒，占 34.20%**，而网络目录只有 276/2057（13.42%）个 C owner。前20最慢文件贡献累计 351.975 秒（6.66%），前100贡献 1,123.677 秒（21.26%）。

尚未在本轮测试的高成本文件：`net/core/dev.o`（25,026ms）、`net/ipv4/tcp_input.o`（24,867ms），以及输入只有约 2.61MB、却耗时 15,609ms 的 `lib/crc32.o`。下一轮应挑大网络文件 + 异常高单位输入耗时的 crc32 作为不同结构的反例，对阶段 `parse / normalize / core-set / core-validate / layout / codegen` 做实际分段归因。优先复用已经存在的 `MINIC_BOOTSTRAP_TRACE` 和 GNU 冻结 .i，不再无目的地为 Boolean 增加重复整套对照。

**决策规则：** Boolean 算法从热点研究队列移入“候选晋级与扩展正确性回归”队列；高耗时文件的下一轮分析必须评估另一种实际瓶颈，不把已经得到明显收益的单一算法研究成没完没了的局部项目。

## 13. PERF-008：六阶段准确归因已执行，明确下一种优化的工作量（2026-10-14）

**Run #38596300095 SUCCESS**，gcc-single 阶段归因，artifact **#11910760375**。原有三个已经验证过的 Linux .i 和 Boolean Candidate .o **直接 SHA 缓存命中**，日志 `COHORT_CANDIDATE_CACHE=HIT objects=3`、`BOOL_HOT_FROZEN=CACHE_HIT owners=3`，没有重新编译候选 Linux C 对象。复用编译器自身早已存在的 `MINIC_BOOTSTRAP_TRACE` 六个边界（parse、normalize、core-set、core-validate、layout、codegen），新增通用 `tools/ci/linux-perf-phase-trace-v1.py` 只负责读取 stderr 行并作单调时钟归因；M0 #38596007530、#38596211193 均通过。两臂真实 .i 的生成汇编 SHA 仍完全一致。

| 实际文件 | 原 Runtime 单次诊断 wall | Boolean 单次诊断 wall | 候选 Parser | 候选 Core-set | 候选 Codegen | 候选六阶段外剩余 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| kernel/bpf/btf.o | 12,129.75ms | 6,887.63ms | 2,963.89ms | 632.85ms | 99.58ms | 3,127.30ms |
| kernel/bpf/verifier.o | 13,006.57ms | 7,911.88ms | 3,100.57ms | 1,249.96ms | 549.00ms | 2,941.80ms |
| net/core/filter.o | 27,796.05ms | 16,563.37ms | **3,892.65ms** | **2,908.38ms** | **2,890.53ms** | **6,793.50ms** |

上述单次诊断 wall 与上一轮四次交替配对的 P50 数据**不是同一种测速口径**，不据此宣传新的加速倍数。六阶段外残差包括编译器读取、解析后验证、特化、Inline 引用计算、对象清理、进程启动等尚未单独界定的步骤，绝不能把全部残差猜成某个阶段。

**最重要的因果信息：** 三份输入旧版→新版的六阶段以外残差减少约 5.19s、5.03s、11.22s，而六个已标注阶段的时间变化很小。这支持“Boolean Domain 缓存优化主要改善已有追踪边界之外的反复工作”这一方向，但并**未证明**具体是哪段函数。不能为了追求过度细分继续钻这个已经拿到近 2× 的算法。

**下一轮工作切换：** 不再重复运行这一份 Boolean A/B。要扩展对尚未优化的高耗时路径做定量分析：`net/core/filter` 优化后 Parser 约 3.9s、Core-set 约 2.9s、Codegen 约 2.9s，都是与布尔域扫描不同的成本中心。优先用已有 Parser lookup counters 查具体符号查询次数/迭代次数（`tools/ci/apply-perf-parser-lookup-counters-v0.py`），再选择现成函数/声明索引的**单变量**实现；同时以 `net/core/dev`、`net/ipv4/tcp_input` 或 `lib/crc32` 作为其他程序结构的慢例/对照。只有证实一个新通用处理算法导致明显耗时下降，才把它加入 Runtime 新编译器候选并进入完整 Linux+QEMU 正确性认证。

**总结：** 当前已为本轮锁定可复用的真实输入 SHA、原生对象缓存和阶段时间数据。下次不用任何重新采集全量，更不应重新编译 2057 个 C 对象来做 Parser 微基准。

## 14. PERF-009：Parser 函数名索引作为独立算法验证（2026-10-15）

- GitHub Actions **#38653742842 SUCCESS**（commit `eb27f0d`），`gcc-single` 只处理已有三份 SHA 固定的 Linux 6.6.143 Kbuild `.i`，恢复了已认证 GNU 参考对象与 Boolean Runtime 候选缓存；**没有重新编译完整 2064 C objects、没有链接和 QEMU**。
- A/B 两臂都打开相同的 Parser 查找计数器和既有 Boolean Domain 索引，只改变**函数名搜索线性正序 vs first-match 保序哈希表**。当前 Parser 的 `find_function` 是正序扫描，最早同名声明获胜。历史 function-hash patch 误按倒序覆盖已由 `b8e4626b61` 修复；函数重复声明保留最早 ID，分配失败回退原始正序扫描。
- 同 runner 每臂热身一次 + **2 次**交替配对（仅 screening，不是先前 Boolean 的 4 次认证），每对 `.s` SHA **字节一致**：

| 真实 owner | 有计数器的线性函数查找 P50 | 函数哈希 P50 | 速度比 |
| --- | ---: | ---: | ---: |
| `kernel/bpf/btf.o` | 5596.636ms | 4245.630ms | **1.3182×** |
| `kernel/bpf/verifier.o` | 6631.175ms | 5384.342ms | **1.2316×** |
| `net/core/filter.o` | 14228.801ms | 12593.635ms | **1.1298×** |

- 三份输入的局部工作量合计 P50 约 26.457→22.224s，局部减少约 4.23s。此前 Boolean 没有查找计数器的多轮数据**不可直接与此相除**，且两项局部 speedup 不能相乘宣称完整 Linux 提速。
- 判定：`verified-local-screen-2x`，明显独立增益但尚需覆盖更多 C 语义情形/不同文件与 P1。此算法进入候选晋级池；不为它无限再跑三份相同输入。
- 下一轮 **PERF-010** 只测试剩余全局对象线性查找：保持函数哈希在双臂开启，现成 `apply-perf-parser-global-hash-v0.py` 作单变量候选。该旧实验脚本原来对重复全局声明执行 last-wins，并在索引同步失败时错误返回未找到；现已修改为 **first-declaration wins**、线性保序 fallback、容量溢出保护。新局部实验在 **#38655550585**，只调用已有 `gcc-single`，三份已冻结输入每臂仅 2 次，预算约束不重新跑 2057/2064。只有实际 A/B 与完全相同汇编输出同时通过才评估收益。正式 Runtime 和 P1 仍保持不变。


## 15. PERF-010：全局名哈希对已启用函数哈希的额外贡献（2026-10-15）

**真实结果：** #38655550585 `SUCCESS`，三份 frozen GNU Kbuild `.i` 均与旧函数哈希候选生成**字节相同的汇编**；`gcc-single` 读取 SHA-bound 3 对象及冻结输入缓存，没有重新生产 2064 个对象；M0 审计对工作流改动首次发现清单 SHA 漂移，已按审查结果更新清单并由 **#38655644589 SUCCESS** 复验。这个成功仅是局部 A/B；没有新的 Linux QEMU P1。

| Linux owner | 两臂都有函数哈希、无全局哈希的 P50 | 另加全局哈希 P50 | 边际 speedup |
| --- | ---: | ---: | ---: |
| `kernel/bpf/btf.o` | 5562.565ms | 5354.064ms | 1.0389× |
| `kernel/bpf/verifier.o` | 6576.720ms | 6403.613ms | 1.0270× |
| `net/core/filter.o` | 14603.246ms | 14748.632ms | **0.9901×（退化）** |

合计中位秒数约 26.7425→26.5063，仅节约 **236.22ms，约 0.88%**。每臂仅 2 次独立热身后交替配对，不能证明 2–4% 小幅差异稳定。尽管保持前向 first-declaration 语义、哈希分配/同步失败时回退正序线性查找、全部汇编一致，此候选未达到此前规定的“多个热点显著收益”准入阈值。

**决策：** `verified-local-but-not-promoted`。**停止继续打磨 Parser 全局哈希**，不启用在 Runtime，也不为了补一个漂亮数字继续扩大同组测试。保留补丁和负面结果作为今后对其他大规模 TU 的参考。下一轮从现有六阶段 `Core-set` 的 0.63/1.25/2.91s 中寻找新的算法目标；历史性能分支已经存在 `apply-perf-core-lower-statement-workspace-v1.py`，先检查适配当前 Runtime 树，不得盲目原样套用旧脚本（现有 Runtime 源码已经移除旧 `local_integer_constants` 辅助结构）。


## 16. PERF-011：从 Parser 轮换到 Core IR 语句块索引（2026-10-15）

**结构假设：** 当前 `src/core/core_lower.c` 的 `minic_core_lower_function` 对每个有函数体的函数都基于**整个 TU 的** `program->statement_count` 新建、初始化 `statement_blocks` 数组。若该 TU 含 F 个函数与 S 个语句，则仅这部分工作就接近 O(F×S)，与每个函数实际用到的语句数不成比例。对于 filter 的 Core-set 约 2.91s 等阶段数据，这是可以证伪的通用算法候选，但**此时尚不是已证明根因**。

**复用而非重造：** 仓库早已保留 `tools/ci/apply-perf-core-lower-statement-workspace-v1.py`，按整个 TU 建立一次映射，仅记录当前函数 touched statement IDs，在函数结束后清理这些槽位；旧 `minic_core_lower_function` API 保持，workspace 分配失败时采用原始 per-function 入口。实验固定 Boolean Runtime 编译器，两臂只变化这一 Core lowering 工作区算法，还是原 `gcc-single`/frozen BPF-net 三份 Kbuild `.i`、两次交替配对及逐字节汇编 SHA。

**一次已记录的设施失败：** #38656415436 的 Runtime `gcc-single` 局部实验第一轮**失败**，失败发生在 patch anchor 校验（非 Linux 编译器语义验证），日志显示 profile 编译器应用前置 Runtime 补丁后，`core_lower.c` 包含 `local_integer_constants`，但最初误依据仓库中原始的未打补丁源码删掉了脚本中的该名称。源树静态版本**不能**代替 CI 中已合成的实际 Runtime profile。这次没有声称任何加速，缓存的三 Linux 对象均已通过原有认证。原因与修复已确认：保留与运行时 profile 匹配的原始锚点，只额外保留 workspace allocation 的线性回退，commit `62e372f`。

**真实下一次运行：** #38656598585，原有 Owner、Kbuild、对象缓存及触发边界不变；该 Run 的局部 verdict 仅在 A/B 输出字节相同且统计合法时可判 `verified-local`。若仍失效先看具体源码锚点/生成 Runtime patch 组合，不能复制所有 2057/2064 个对象重跑。该算法若收益不明显即停止，轮换到新的实测高耗时阶段。

**PERF-011 最终反馈（2026-10-15）：** 修复后的 CI **#38656598585 SUCCESS**，Boolean Runtime 与 Boolean+Core statement workspace，两臂各热身一次+仅 2 次配对；三份真实冻结 Linux Kbuild `.i` 的汇编 **逐字节一致**，原始对象缓存命中。具体 P50：

| owner | 原 per-function statement map | per-TU workspace | speedup |
| --- | ---: | ---: | ---: |
| `kernel/bpf/btf.o` | 8235.989ms | 8159.755ms | 1.0093× |
| `kernel/bpf/verifier.o` | 9704.757ms | 9226.913ms | 1.0518× |
| `net/core/filter.o` | 18785.012ms | 18640.775ms | 1.0077× |

三个文件的中位耗时和约 **36.726→36.027s**，共节约约 **0.698s / 1.90%**。实际文件可能并非这类语句清零的最病理输入，但三个主要热点没有发现明显的两位数百分比改善。**局部正确性通过、性能未达晋级阈值**，状态 `verified-local-not-promoted`：保留 opt-in 脚本和失败/成功记录，**不将其加入 Runtime 正式默认配置，也不继续对这三个 TU 重复测同一工作区算法**。

**下个轮转目标：** 现有阶段跟踪显示 filter Codegen ~2.89s，而 Boolean 后还有多个未充分分解的阶段。优先在原 frozen `.i` 上选现成的 Codegen 使用次数/IR 功能级诊断，确认下一项真正重复遍历/过量分配的耗时路径。旧版 `apply-perf-core-object-address-use-index-v1.py` 等候选只能在核对已合成 Runtime profile 的源码结构、确认当前未已有同等索引、确认没有重复历史优化后，才有理由做下一项 A/B。暂不应以“再添一个哈希”替代实际数据。


## 17. PERF-012：Codegen OBJECT_ADDRESS UseId 反复扫描 → 一次建索引（2026-10-15）

**为什么换方向：** PERF-010 的全局哈希额外总收益 <1%，PERF-011 的 Core lowering 工作区在三个慢文件上约 1.90%，均未达到强收益阈值。旧的六阶段证据指出 filter Codegen ~2.89s；与已修复过的 blake2s `first_access_defines`（块内生产者位置缓存）**不同**，Core `OBJECT_ADDRESS` 值使用统计仍会对每个对象扫跨块 IR。故复用已存在 `tools/ci/apply-perf-core-object-address-use-index-v1.py`（P14），尝试构建 ValueId 使用次数/直接访问范围索引，索引不可用时回退原查找。仅 P14，**没有启用 P15 scalar interval index**。

**实验事实：** [GitHub CI #38657320806](https://github.com/yituanxing/minic-toolchain/actions/runs/38657320806) SUCCESS，旧版为原已认证 Runtime+Boolean 编译器，候选仅增 P14。复用三个 SHA-frozen Kbuild 输入和原有 GNU/Boolean 候选对象缓存；每臂热身一次 + 2 次交替配对，所有 `.s` 字节完全一致。M0 [#38657320831](https://github.com/yituanxing/minic-toolchain/actions/runs/38657320831) 同时 SUCCESS，工作流与源清单 SHA 以同一原子提交 `375d062a` 更新，无额外 workflow/job。

| Linux owner | Boolean 基准 P50 | Boolean + P14 P50 | 节省 | 倍数 |
| --- | ---: | ---: | ---: | ---: |
| `kernel/bpf/btf.o` | 7530.087ms | 7483.638ms | 46.449ms | 1.0062× |
| `kernel/bpf/verifier.o` | 8758.617ms | 8244.811ms | 513.806ms | 1.0623× |
| `net/core/filter.o` | **17868.301ms** | **15163.615ms** | **2704.686ms** | **1.1784×** |

三个真实热点局部编译时长中位数之和 **34.157→30.892s**，累计节省 **3.265s**（约 **9.56%**），其中 filter 一个 owner 就贡献约 83% 的收益。这个结构更适用于具有大对象地址使用/复杂 IR 的网络长尾，而非对每个 TU 都同样有加速。输出相同仅证明这三个输入，且 **2 次/臂是快速筛选，不能作为稳定全量性能倍数或宣称 Linux P1**。

**结论和下一轮：** `verified-local-screen-2x / promotion-candidate`。达成先前“未优化过的最慢文件有可重复的两位数候选改善”的**初筛数值门槛**，但“可重复”仍需 4 次交替或更广泛输入重新验证。将 P14 移入**扩大样本与正确性晋级队列**；不要立刻重复微调 P14/P15，把已有两个几乎没有增益的 Parser global 和 Core workspace 继续保留为不晋级的负例。

因果假设为把每 `OBJECT_ADDRESS` 的完整 IR 扫描改成 per-function 一次建立 ValueId 使用索引；**尚未**由独立函数级 CPU 计时证明节省的 2.705s 全部来自 Codegen 具体子函数。后续选更慢的 `net/core/dev`、`net/ipv4/tcp_input` 和一个同大小更快的对照，固定输入和新编译器 SHA，先做小规模输出/ABI，达到足够范围后才使用已经存在的七路全量和 QEMU P1。仍然不动当前 Runtime 默认编译器与认证 Image。


## 18. PERF-013：P14 扩展到新的三种 Linux 长尾结构（2026-10-15）

**已实施：** [commit `ee224620`](https://github.com/yituanxing/minic-toolchain/commit/ee224620d815973129eee23c778befd41597a36c) 在旧 `linux-runtime-gcc-baseline.yml` `gcc-single` Owner 内增加互斥的第二组 3 个真实 Linux Kbuild C 对象：
- `net/core/dev.o`：历史 MiniC wall ~25.03s，网络设备与大量控制分支；
- `net/ipv4/tcp_input.o`：历史 ~24.87s，TCP 状态机与复杂条件；
- `lib/crc32.o`：历史 ~15.61s，源码/数据结构与前两者不同的对照。

原 BTF/verifier/filter 三对象 SHA、Frozen `.i`、优化结论和 cache key **完全保持不变**；新组在相同固定 Linux .config 与 GNU 黄金对象下建立**独立**3-owner manifest/cache，且在 `cohort` 三个允许名单处都做精确目标身份检查。Candidate 测试只增加已初筛 P14 Core ValueId 使用索引；保留 Boolean Runtime，其余算法保持不变。新三组配对仍是两次/臂初筛，要求各 `.s` SHA 一致；随后根据跨程序结构收益决定是否追加 4 次/臂或 500 TU 回归。

**历史重用审计：** 找到已完成的 Performance Top5 [#37888313913](https://github.com/yituanxing/minic-toolchain/actions/runs/37888313913)，其中 **P15 interval-onepass 在 P14 已开启的工具版本上**，5 个真实 Linux TU 15/15 字节等价，但累计 P50 仅 **6.6855→6.4662s（1.0339×）**，其中 memcontrol 有退化。这个实验工具配置不等同于当前 Runtime，不能直接把 P15 称为无效；不过依照轮换/28 规则，**没有必要立刻围绕 P14 再做一次 P15 Top5**，先检查新结构下 P14 是否有普遍收益。

**已发现并改正的一次实验设施失败：** 首次新 owner 扩组 [#38658207641](https://github.com/yituanxing/minic-toolchain/actions/runs/38658207641) 对 3 个新 owner 的 GNU golden 对象/config Kbuild 预检已 PASS，但在 Kbuild 中间产物保留检查中被旧硬编码 BTF/verifier/filter allowlist 拒绝：`BOOL_HOT=FAIL unexpected_owner_set`，尚未真实执行新对象的 candidate 编译或 A/B。代码审查后精确改正这**第三个**原 owner 名单，保留严格 allowlist（不放宽至任意 owner），fix `6a14f58b`。

**独立修复复跑：** [#38658389460](https://github.com/yituanxing/minic-toolchain/actions/runs/38658389460)；按此记录时仍在运行。不可把 CI started / cache MISS / M0 SUCCESS 误记为性能验证成功。若新输入正确性或时间数据不合格，应记录为显式失败，不能自动晋级 P14，也不能无依据重启完整 2064 对象。


**PERF-013 真实轮转结果（2026-10-15）：** [#38658389460](https://github.com/yituanxing/minic-toolchain/actions/runs/38658389460) `gcc-single SUCCESS`，恢复不可变 GNU 黄金缓存后只编译了三个独立的新 MiniC 候选对象（`COHORT_FULL_COMPILE attempted=3 compiled=3 failed=0`、`COHORT_ABI=PASS objects=3`），并保存新的原始 Kbuild `.i` SHA 锁定语料。仅新增 P14、同 runner 双臂各热身一次和 **2 次配对**，每一文件生成汇编字节一致：

| 旋转 owner（此前从未在 P14 测量） | 原 Boolean Runtime P50 | Boolean + P14 P50 | 倍数 |
| --- | ---: | ---: | ---: |
| `net/core/dev.o` | 16073.989ms | 16105.739ms | **0.9980×（轻微退化）** |
| `net/ipv4/tcp_input.o` | 11471.151ms | 10996.026ms | 1.0432× |
| `lib/crc32.o` | 19463.595ms | 18903.915ms | 1.0296× |

三份新长尾累计 P50 **47.009→46.006s，节省约 1.003s / 2.13%**。加上之前 3 个文件 P14 初筛 3.265s 的收益，**六个文件对应两次不同 CI 的观测中位数之和**约 **81.167→76.898s / 5.26%**；这只能作为各自同条件局部数据的算术合并，不能说整个 Linux 5.26% 提速，也不能消除跨 Runner 差异。

**决策：** P14 在 filter 上有明确收益，但扩展至这三类程序结构时较弱，因此继续保留为 `scoped-promotion-candidate` 而**不是无条件默认启用**。当前最大慢文件 dev / crc32 的主要耗时显然不在 P14 所消除的扫描内。按照瓶颈轮转方法，不再反复细抠 P14；下一步复用三份**新缓存**的 `.i` 对此前在 BPF/filter 上明显有效的独立函数名哈希做单变量 A/B，判断它能否对 dev / tcp_input / crc32 的 Parser 规模产生通用收益。使用原先 `[linux-perf-parser-index]` 入口及旧 Parser 查找计数器，不新建 workflow，不重新 GNU Kbuild 生成 `.i`，修订后的路由只对 `[linux-perf-hotowners-rotation]` 改换三个 owner。


## 19. PERF-014：函数名哈希扩展到第二组 3 个真实 Linux 长尾（2026-10-15）

[CI #38659189580](https://github.com/yituanxing/minic-toolchain/actions/runs/38659189580) `gcc-single SUCCESS`。**之前没有用于函数哈希验证的 3 个** Linux 6.6.143 GNU-Kbuild `.i` 被独立 SHA 验证并从上一轮 P14 的既有缓存恢复，日志 `COHORT_CANDIDATE_CACHE=HIT objects=3`、`BOOL_HOT_FROZEN=CACHE_HIT owners=3`。不再编译新 Linux .o，只增量构建 MiniC-cc 两个版本。固定同一 Boolean Runtime 与 lookup counter instrumentation，单变量仅改变 `minic_parser_find_function` 正序线性查找与 earliest-first 哈希索引；保持 local/global/typedef 不变，每臂独立热身一次 + 两次交替配对，**每个输入两臂生成汇编逐字节相同**。

| 新 Linux 输入 | 有计数器的线性函数查找 P50 | 函数名保序哈希 P50 | 局部 speedup |
| --- | ---: | ---: | ---: |
| `net/core/dev.o` | 14754.400ms | 12581.869ms | **1.1727×** |
| `net/ipv4/tcp_input.o` | 10125.323ms | 8105.407ms | **1.2492×** |
| `lib/crc32.o` | 16429.620ms | 15993.317ms | **1.0273×** |

三份新结构的独立 P50 合计 **41.309→36.681s**，约节省 **4.629s / 11.20%**。前面第一组 BTF/verifier/filter 的函数哈希节省约 4.23s（PERF-009），这说明独立 Parser 函数名索引在 BPF/net 的**多种规模 C 文件中都有两位数收益**，但在 `crc32` 很有限，证实不能把一个算法强行推广到所有文件。**两组 2 次/臂初筛、不同 CI Runner，不是完整全量基准**。

**判定：** `verified-local-six-owners / correctness-promotion-candidate`。函数哈希比 P14 更广泛改善这批慢文件，但尚未验证更多 C 语义边界（重声明 earliest-first，失败 fallback，作用域）、first500 及 QEMU P1，正式 Runtime 仍不切换。不要继续对同六文件无限重测函数哈希；进入扩大语义和正确性覆盖队列。

## 20. PERF-015：第二批结构化阶段归因，寻找函数哈希以外的最大瓶颈

[CI #38659484515](https://github.com/yituanxing/minic-toolchain/actions/runs/38659484515) `SUCCESS`。复用此前同组 SHA-frozen 输入、Boolean candidate 与 GNU cached .o。现有 `MINIC_BOOTSTRAP_TRACE` 六段，不加新的编译器源码优化，在单次诊断中两臂汇编相同。**只记录同一 CI 的阶段计时，不与独立 P50 A/B 拼成同条件速度比。**

| 真实文件 | Boolean 后 Parser | Core-set | Codegen |
| --- | ---: | ---: | ---: |
| `net/core/dev.o` | **4901ms** | 2195ms | 248ms |
| `net/ipv4/tcp_input.o` | **4208ms** | 1212ms | 311ms |
| `lib/crc32.o` | **810ms** | 116ms | **9ms** |

大网络文件的 Parser 仍是已标注的主要阶段；`lib/crc32` 反常：虽然总编译耗时在独立实验中约 16–20s，但六阶段累计不到 1s，**绝大多数耗时落在六阶段之外**。这一现象明确解释了为什么 P14 和函数哈希对 crc32 几乎没有贡献，但不能把残差直接认定成某个函数。

**立即采用更经济的诊断：** 已有 `linux-perf-phase-trace-v1.py` 的 stderr monotonic timestamps **本来就有六阶段 begin/end 完整时间线**，过去仅汇总六段丢弃中间间隔。Commit `b019f840` 在原脚本里增加 **五个间隔区间与 pre/post 残差** 的读数，不修改任何 MiniC 生产算法、不新建 CI 工作流。独立 opt-in [CI #38660104046](https://github.com/yituanxing/minic-toolchain/actions/runs/38660104046) 对同组缓存复采，用其具体 gap 数据来缩小 `crc32` 可能的昂贵逻辑，例如 AST verification 或 inline-emission reference recomputation 等**待验证假设**。该 Run 未结束前不得声称已定位根因。


## 21. PERF-016：真实耗时根因从六大阶段缩小到四类内联专门化（2026-10-15）

**来源：** [六阶段间隔定位 CI #38660104046](https://github.com/yituanxing/minic-toolchain/actions/runs/38660104046) 和 [四子阶段细分 CI #38660602600](https://github.com/yituanxing/minic-toolchain/actions/runs/38660602600)，两者 `gcc-single SUCCESS`，三份已 SHA 固定的 GNU-Kbuild `.i` 及缓存复用；测试前后 `.s` SHA 完全一致，运行的是诊断插桩，不是生产优化、也不是 QEMU P1。

发现：旧 `parse/normalize/core-set/core-validate/layout/codegen` 统计之和远小于命令执行 wall，绝大多数耗时集中在 `normalize end` 和 `core-set begin` 之间。对真实 `net/core/dev.o`、`net/ipv4/tcp_input.o`、`lib/crc32.o` 分别约为 **8.175s、5.287s、16.767s**。使用仓库**既有** `apply-perf-specialization-phase-trace-v0.py` 对 compiler.c 插入 spec-integer、spec-symbolic、spec-boolean、spec-finalize 四处计时；所有环节仍字节一致。

| Linux owner | `spec-integer` | `spec-symbolic` | `spec-boolean` | `spec-finalize` | normalize→core-set 全间隔 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `net/core/dev.o` | 3303.41ms | 4444.91ms | 233.77ms | 76.49ms | 8174.99ms |
| `net/ipv4/tcp_input.o` | 2184.00ms | 2783.83ms | 166.31ms | 43.91ms | 5287.10ms |
| `lib/crc32.o` | 9113.36ms | 7542.68ms | 23.34ms | 45.79ms | 16766.71ms |

关键结论：`lib/crc32` **不是** Codegen 慢，而是大量整数内联专门化与符号内联专门化；这两者已解释这段空白的 ~16.66s。网络两文件则也是整数与符号内联耗时远高于 Codegen。**不再继续优先在已只有毫秒级耗时的 crc32 Codegen 上研究 P14/P15**。已有 `apply-perf-symbolic-loop-trace-v0.py` 还能把符号专门化分为 direct/transitive 两部分；已有 `apply-perf-symbolic-function-body-owner-filter-v0.py` 则是带回退的 owner 过滤候选，但应先验证是否仍适用于现行 Runtime profile，并做数据驱动、字节等价的独立 A/B。

另：#38660104046 的一次六阶段间隔单次 old/new 比较中，在没有相应算法变化的条件下 `normalize-to-core-set` 对 dev/tcp 表现出十多秒波动，因此**不得**将该诊断计时差当成优化提升；应仅用同次细分计时确定瓶颈，并在下一轮至少配对复测后宣称速度收益。现有正确性基线与原工具链 QEMU/Runtime 一概不动。


## 22. PERF-017：符号内联真实瓶颈和首轮算法优化（2026-10-15）

[分阶段 CI #38662744051](https://github.com/yituanxing/minic-toolchain/actions/runs/38662744051) SUCCESS，三个 Linux Kbuild 冻结源的 `spec-symbolic` 用时几乎全部来自 `transitive`，而不是 `direct`：dev 15ms / 4734ms；tcp_input 11ms / 2705ms；crc32 4ms / 7685ms。编译汇编逐字节相同。阶段追踪的残差曾因 nested timer 重复计入而为负，`4b7574e` 已修正求和；不以该残差判断优化。

[第一轮优化 CI #38662578838](https://github.com/yituanxing/minic-toolchain/actions/runs/38662578838) SUCCESS。复用历史 `apply-perf-symbolic-function-body-owner-filter-v0.py`，基于已验证的 FunctionBody 源函数归属，在传递循环中排除明显不属于该源函数的表达式。内存分配/索引建表失败退回原始扫描。两次/臂、各热身一次，三份真实 `.i` 汇编每次完全相同。

| owner | Boolean 原编译 P50 | 归属索引 P50 | speedup |
| --- | ---: | ---: | ---: |
| lib/crc32.o | 17700.280ms | 12491.452ms | **1.4170×** |
| net/core/dev.o | 15714.800ms | 11426.814ms | **1.3753×** |
| net/ipv4/tcp_input.o | 11207.556ms | 8757.871ms | **1.2797×** |

局部 P50 合计 44.623→32.676s，节约 11.947s（约 26.77%）。`verified-local-2-repeats`；只说明三个真实输入的实验性能与字节一致性，不代表整体 Linux 提速，更不代表新增 QEMU P1 已通过。该候选明显有收益，进入扩样验证队列，不继续调小百分比算法。现有正式 Runtime 仍不启用此候选。


## 23. PERF-018：符号归属索引的独立扩样（2026-10-15）

[CI #38663324978](https://github.com/yituanxing/minic-toolchain/actions/runs/38663324978) 成功，复用原有三份 SHA 固定的 BPF/filter 真 Linux .i；Boolean 基准 vs 仅加符号归属索引，每臂热身一次、配对两次，汇编输出逐字节相等。

| 文件 | 基准ms | 优化ms | 倍数 |
|---|---:|---:|---:|
| btf | 5721.173 | 4322.472 | 1.3236× |
| verifier | 6418.995 | 5139.084 | 1.2491× |
| filter | 13912.770 | 11224.488 | 1.2395× |

至此两批共六个真实 Linux TU 初筛 6/6 字节一致且全部加速 1.24～1.42×。仍属局部候选，未全量 Linux 或 QEMU P1 认证。


## 24. PERF-019：整数传递专门化 CALL-only 扫描实验（2026-10-15）

[CI #38663509948](https://github.com/yituanxing/minic-toolchain/actions/runs/38663509948) `gcc-single SUCCESS`。固定 Boolean Runtime 原件与已缓存 3 个真实 Linux Kbuild `.i`，候选只复用 `tools/ci/apply-perf-integer-call-expression-index-v1.py`：把每个克隆函数反复遍历 TU 全部 expression 的逻辑改为“一次扫描保存 CALL expression ID，之后克隆只看这些 CALL”；索引内存不足则回到原有全扫描，CALL 遍历顺序不变。编译与对象身份缓存均命中，逐字节汇编一致，每臂热身一次 + 两次交替配对。

| owner | Boolean baseline P50 | Boolean + integer CALL-index P50 | speedup |
| --- | ---: | ---: | ---: |
| lib/crc32.o | 21338.163ms | 17285.939ms | **1.2344×** |
| net/core/dev.o | 17876.262ms | 16994.266ms | 1.0519× |
| net/ipv4/tcp_input.o | 12570.327ms | 11969.206ms | 1.0502× |

合计 **51.785→46.249s**，节省约 5.535s（约 10.69%），其中 crc32 最明显。判定 `verified-local-3 / keep-as-candidate`；绝不能宣称 Linux 整体 +10.69%。

**下轮：** 与 PERF-017 符号源归属过滤共同开启，**先做 incremental marginal A/B：Boolean+symbolic-owner vs Boolean+symbolic-owner+integer-CALL-index**。能保持 3 个真实文件逐字节一致且 crc32 仍保留显著额外收益，才考虑向 first500/7-runner 递进。未完成全面兼容性/QEMU P1，不直接晋级已认证 Runtime。


## 25. PERF-020：两个已独立有效索引的联合边际 A/B（2026-10-15）

在原 Runtime+Boolean 基线上，保持 **符号源函数归属过滤同时开启于两臂**，唯一改变整数传递专门化 CALL 表达式索引是否开启，测试了它们是否争夺同一热点。CI [#38664438661](https://github.com/yituanxing/minic-toolchain/actions/runs/38664438661) `gcc-single SUCCESS`，两臂各热身一次 + 2 次配对，三份 SHA 冻结的真 Linux Kbuild `.i`，`.s` 完全字节一致，重用了 3 个 golden 和 candidate 缓存对象。

| 文件 | Boolean+symbolic owner P50 | 再加 integer CALL P50 | 后者额外收益 |
| --- | ---: | ---: | ---: |
| lib/crc32.o | 8770.894ms | 7562.590ms | **1.1598×** |
| net/core/dev.o | 9360.208ms | 8812.370ms | 1.0622× |
| net/ipv4/tcp_input.o | 7102.372ms | 6876.769ms | 1.0328× |

合计 **25.234→23.252s**，额外节省约 **1.982s（7.86%）**；两个优化并非完全重叠，`crc32` 是主要获益者。但这依然是本组同-run 2× 快筛，**不等于完整 Linux 7.86%**。状态 `verified-local-cohort / integer-optional`。符号归属过滤已在独立 6 TU 上有 1.24～1.42×，相比之下整数索引边际小且更集中，优先为符号过滤做**完整 500 TU 等价认证**，不要为了小尾巴继续优化整数索引。

相应 [CI #38664911462](https://github.com/yituanxing/minic-toolchain/actions/runs/38664911462) 仅做严格正确性：尝试直接恢复现成 SHA-frozen first500 corpus（命中前不下载或重生成），固定原 Boolean Runtime，两臂仅差符号归属过滤，4 worker 每输入双方只编译一次并 SHA 比较。允许的结论是 `selected=500 identical=500`，一处编译错误或汇编不同即记为不通过；不是性能测试，更不是 QEMU P1 认证。当前仍待最终 CI verdict。

## 26. PERF-021：符号传递专门化 CALL-only 候选及受控失败修复（2026-10-15）

在六个 Linux 文件的 `FunctionBody` 来源归属索引表现为 **1.24～1.42×** 后，仍可见 transitive 循环遍历整个 TU 所有表达式。现有整数版 CALL-ID 索引已有 3/3 字节等价，故仅以新 opt-in `tools/ci/apply-perf-symbolic-call-index-v1.py` 加到 **Boolean + symbolic-owner**，不混入其他算法。候选先收集原始顺序的 CALL expression ID；索引分配失败或表达式数量异常时回退原扫描，并保留失败路径内存清理。再次复用 SHA-frozen crc32/dev/tcp_input，双臂各热身一次+两轮配对。

首次 [CI #38665239996](https://github.com/yituanxing/minic-toolchain/actions/runs/38665239996) **失败且没有 A/B 数据**：可读日志定位到 Python patch 的源码锚点错误 `symbolic transitive scan anchor count != 1`；此前统一全局检查错误地把其他函数里的同形表达式循环也计入，失败在 **源文件改写阶段**，并非编译器语义或生成汇编失败。commit `d0a5b032` 将锚点限制到 symbolic transitive 代码块前后，保留原所有语义路径。隔离复跑 [#38665476648](https://github.com/yituanxing/minic-toolchain/actions/runs/38665476648)，以其最终 `MINIC_ONE_OWNER_AB=PASS` 和性能为判据；如无实质改善就停止，不对同项继续小修。

另外新增 `tools/ci/linux-perf-frozen-corpus-differential-v1.py` 的 [CI #38664911462](https://github.com/yituanxing/minic-toolchain/actions/runs/38664911462)，严格恢复既有 500 个 SHA-frozen GNU Kbuild `.i`，而非重新下载内核。每个真实 TU 都由原 Runtime+Boolean 与仅加 symbolic-owner 两种编译器编译，独立 SHA 对比 `.s`，只在 **500/500 字节相同且均成功** 时宣称局部 500 份语义表象认证；这不是 Linux 全量运行时 P1。两条 CI 未得最终判定之前不可把它们写成 `PASS`。


## 27. PERF-022：符号源归属索引 first500 严格字节等价通过（2026-10-15）

[CI #38664911462](https://github.com/yituanxing/minic-toolchain/actions/runs/38664911462) `gcc-single SUCCESS`。基线为正式 Boolean Runtime，唯一差异是 opt-in FunctionBody 表达式来源归属过滤。直接恢复既有、500/500 输入 SHA 校验的 Linux 6.6.143 RISC-V GNU Kbuild `.i`；严格以两个不同 SHA 的 MiniC-cc、同一 500 文件双边 `-S` 成功、`.s` SHA 字节完全一致作为标准。日志按每 50 个记录进度，最终 `MINIC_FROZEN_DIFF=PASS selected=500 identical=500 runtime_p1_not_proven=true`。**这是明确的 500/500 编译输出等价验收，不是整内核链接/启动认证，也不是任何加速测量**。历史 6 TU 两次配对性能 1.24～1.42× 保持独立证据。符号归属索引晋级为 `500-input-assembly-equivalent / QEMU-p1-pending`，可进入原既有七路 Linux/Kbuild 运行时分级认证。

## 28. PERF-023：已有符号归属索引上进一步减少 CALL 遍历

候选 `apply-perf-symbolic-call-index-v1.py`，在两臂**均开启**已验证 symbolic-owner 时，仅候选额外提供 stable ExpressionId 顺序的 CALL-only 索引。初次 [#38665239996](https://github.com/yituanxing/minic-toolchain/actions/runs/38665239996) 仅在 patch-anchor 阶段失败，已记录源匹配修复。复跑 [CI #38665476648](https://github.com/yituanxing/minic-toolchain/actions/runs/38665476648) **SUCCESS**、三份真实 frozen `.i` 汇编逐字节一致：

| owner | Boolean+owner P50 | +symbolic CALL P50 | 额外倍数 |
| --- | ---: | ---: | ---: |
| lib/crc32.o | 13149.251ms | 11769.657ms | 1.1172× |
| net/core/dev.o | 11858.853ms | 11701.606ms | 1.0134× |
| net/ipv4/tcp_input.o | 9034.552ms | 8818.987ms | 1.0244× |

累计中位数之和 34.043→32.290s，节约 1.753s，约 **5.15% 边际减时**，主要集中 `crc32`。虽然 3/3 字节等价，这个收益比 symbolic owner 核心优化明显小得多；判定 `local-only / not-promoted`，不再追加 CALL 索引微调或为这一小项重跑 first500，先重新归因 **Boolean+symbolic-owner+integer-CALL-index** 已优化配置上剩余实际最慢阶段（不能用未优化阶段旧数据取代当前瓶颈）。


## 29. PERF-024：在**已优化版本**而非历史版本上重新归因瓶颈（2026-10-15）

审计改正一次真实实验基础设施问题：首轮 [#38666730431](https://github.com/yituanxing/minic-toolchain/actions/runs/38666730431) 工作流初始验证**失败、零 Job**，后续 [#38667001234](https://github.com/yituanxing/minic-toolchain/actions/runs/38667001234) 同样失败；新加的 2538 字符巨型内联 bash 将 gcc-single 的单个 `run` 文本从前一已通过配置的 **24513** 推到 **27051** 字符，触及当前 GitHub Actions 接受的脚本规模边界。将旧专门化实验与新优化后诊断都移入可复用 shell helper，原 24k 内联片段缩至 **约 22.7k**，现有 workflow/job 不增加，M0 清单仍以同一原子提交更新。[#38667153870](https://github.com/yituanxing/minic-toolchain/actions/runs/38667153870) `gcc-single SUCCESS`，证明路由已修复、两个 helper 能跑通。

**新的结构化耗时数据：** 固定既有三份真正 Linux GNU Kbuild `.i`，基准是 Runtime Boolean，候选是 **Boolean + FunctionBody symbolic-owner + integer CALL-ID**，并在两臂**同样添加**四类 specialization 计时（仅诊断，不是生产成本），全部 `.s` 逐字节一致；一次运行的阶段计时如下：

| owner | 候选单次 wall | 候选 Parser | 候选 Core-set | **候选整数专门化** | 候选符号专门化 | 已归类最慢阶段 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| lib/crc32.o | 7735ms | 397ms | 82ms | **5013ms** | 2057ms | **integer** |
| net/core/dev.o | 9000ms | **3079ms** | 2037ms | **2599ms** | 272ms | **Parser**（与 integer、Core-set 均重要） |
| net/ipv4/tcp_input.o | 5987ms | **2418ms** | 1120ms | 1418ms | 232ms | **Parser** |

比较用的原 Boolean 版本同次单次 wall 分别约 16007、12959、8870ms；属于计时插桩的阶段归因，不应和独立多次 A/B 的 P50 相加成产品级 speedup。已优化的网络程序符号 transitive 仅数百 ms，旧热点已经转移；**不能继续执着符号传递循环原样微调**。

**下一两项有代码证据的算法疑点（待计数/验证、不是根因定论）：** 整数内联专门化内会 `for (nested_candidate_index=original_function_count; nested_candidate_index<program->function_count;...)` 每次线性找匹配 variant；另外 Core 在选择最具体 specialization 时也可能按 `function_count` 扫描；这些会形成“候选 cloned functions × 每次 variant 检索”二重工作。需要诊断统计调用次数/检查次数，量化本阶段所占耗时，然后才决定是否做 source-ID scoped lookup/index，保留原始首匹配及回退语义。另一方面 Parser 函数名哈希已在单独两组三 TU 局部提速，下一轮使用 [CI #38667404638](https://github.com/yituanxing/minic-toolchain/actions/runs/38667404638) 对**最新 Boolean+owner+integer** 两臂在有相同 lookup 计数器的条件下做独立 function-hash 边际 A/B，并保持 compiler 输出逐字节相同；以 CI verdict 为准，不假设收益可直接相乘。


## 30. PERF-025：实际联合优化配置下的 Parser 函数名哈希，跨阶段增量收益（2026-10-15）

[真实 A/B CI #38667404638](https://github.com/yituanxing/minic-toolchain/actions/runs/38667404638) `gcc-single SUCCESS`，固定 3 个经过 SHA 校验的 Linux Kbuild `.i`，三份 GNU golden/candidate 缓存均命中。**两臂都有 Boolean+symbolic FunctionBody-owner+integer-CALL index+相同 Parser 查找计数器**，候选唯一额外变化是 `minic_parser_find_function` 的 earliest-first 保序哈希索引，而不是将两个独立实验倍率相乘；各自独立热身 + 2 次交替配对，所有 `.s` 完全逐字节相同。

| owner | 复合基准 P50 | 仅额外增加 Parser 函数名哈希 | 候选增量 speedup |
| --- | ---: | ---: | ---: |
| lib/crc32.o | 11315.361ms | 10936.887ms | 1.0346× |
| net/core/dev.o | 11999.495ms | 9035.768ms | **1.3280×** |
| net/ipv4/tcp_input.o | 8970.781ms | 6479.701ms | **1.3844×** |

三文件 P50 之和约 **32.286→26.452s**，节省约 **5.833s / 18.07%**（限此 3 个文件且受 2 次/臂快筛噪声影响）。说明后续网络慢文件的 Parser 函数符号检索与内联专门化是**可以叠加的两个独立热点**；crc32 剩余瓶颈则仍为 integer (~5.0s) 及 symbolic (~2.1s)，函数名哈希在此仅边际 3.5%。

**决策：** Parser 保序函数名哈希从单模块快筛提升为 `integrated-candidate / independent-bpf-cohort-pending`，**不自动进入已认证 Runtime**。下一条独立 cohort 是 BTF/verifier/filter，继续固定 Boolean+owner+integer，只增加 Parser 哈希 A/B 和 `.s` SHA 对比；回归不能再使用已经测过的 crc32/dev/tcp_input 假充扩样。即使第二组也通过，后续必须对三个算法联合编译器进行 first500 等价验证，并最终 QEMU P1 才能宣称升级正式工具链。


## 31. PERF-026：Parser 保序函数名哈希在完整联合候选上扩展至第二组三 TU（2026-10-15）

[CI #38669659296](https://github.com/yituanxing/minic-toolchain/actions/runs/38669659296) `gcc-single SUCCESS`。与 PERF-025 的 crc32/dev/tcp_input 组**不同**，本组使用已有 SHA 锁定的 BTF/verifier/filter Linux Kbuild `.i`；原始 GNU `.o` 与候选缓存都 HIT，双臂仅函数名 lookup 由 first-definition-wins 正序线性扫描换成 incremental 哈希（两臂符号归属索引、整数 CALL-ID 索引、Parser 计数器全部相同）。每臂热身一次、交替两次配对，三份汇编字节一致。

| owner | 现有 Boolean+symbolic-owner+integer P50 | 再加入 Parser 函数哈希 P50 | 同 runner 边际 speedup |
| --- | ---: | ---: | ---: |
| `kernel/bpf/btf.o` | 7017.063ms | 4873.721ms | **1.4398×** |
| `kernel/bpf/verifier.o` | 8535.362ms | 6297.635ms | **1.3553×** |
| `net/core/filter.o` | 15373.530ms | 12772.778ms | **1.2036×** |

本组局部 P50 合计 **30.926→23.944s**（少用约 6.982s / 22.58%）。结合 PERF-025 的第一组，**六个真正 Linux 文件全部显示 Parser 哈希相对于已优化候选的额外加速，六份汇编等价**。不能把跨-run 倍率直接相乘或宣称完整 Linux 同样提速。验收级别 `verified-local-integrated-six-2rep`；仍未完成**三个优化同时开启的 first500 / 整内核 QEMU P1**。

已在 `gcc-single` 复用旧 `selected-tus.txt`、`inputs.sha256`、500 份 GNU Kbuild `.i` 启动 [#38670218353](https://github.com/yituanxing/minic-toolchain/actions/runs/38670218353)：Boolean-only（固定 Runtime）与 Boolean+symbolic-owner+integer-CALL+Parser 保序函数哈希两版本**严格 500/500 编译与汇编 SHA 校验**；只有实际出现 `MINIC_FROZEN_DIFF=PASS selected=500 identical=500` 才能晋级组合正确性。当前还未获得最终 verdict。

## 32. PERF-027：优化后整数专门化子阶段归因（诊断并行）

PERF-024 定位 **最新 Boolean+symbolic+integer** 版本 `lib/crc32.o` 整数内联专门化约 5.013s，占该 TU 当前 wall 7.735s 中最明显大头。已有源码 `apply-inline-specialization-transitive-integer-v0.py` 中直接阶段含“每次 candidate clone 线性匹配”，transitive 阶段还含“每个已克隆 caller 遍历 CALL 节点 + clone 线性匹配”，因此两处可能有高频 O(clones×candidate-count) 扫描。但未经阶段细分/计数之前不指定责任阶段。

复用原 `minic_bootstrap_trace_stage` 和 `linux-perf-phase-trace-v1.py`，新增 **仅诊断** `tools/ci/apply-perf-integer-loop-trace-v0.py`；现有 `tools/ci/linux-perf-diagnose-optimized-phase-v1.sh` 会在两臂同样插入 `spec-integer-direct` 与 `spec-integer-transitive` 计时，分别核对与外层 `spec-integer` 的计时包含关系，并确保原始 `.s` SHA 一致。使用原三份 frozen Kbuild 输入，保持先观察时间分布、再研究源码定算法、再单变量配对的顺序。诊断 [#38670413942](https://github.com/yituanxing/minic-toolchain/actions/runs/38670413942) 以最终日志判定；未完成前不可称整数新优化已实现或已提速。


## 33. PERF-028：整数专门化的剩余瓶颈确定是 transitive；开始源码归属优化（2026-10-15）

通过 [CI #38670835174](https://github.com/yituanxing/minic-toolchain/actions/runs/38670835174) `gcc-single SUCCESS`，对此前独立 A/B 已验证的 3 个冻结 Linux Kbuild 输入，在 Boolean 与 **Boolean+symbolic owner+integer CALL-index** 两臂插入完全相同的诊断计时，`.s` 逐字节相同。分为 `spec-integer-direct` 和 `spec-integer-transitive`，两段求和严格等于外层 spec-integer。第三次诊断在脚本初始、结束定位锚点修正后实际成功；前两轮 [#38670413942](https://github.com/yituanxing/minic-toolchain/actions/runs/38670413942)、[#38670658498](https://github.com/yituanxing/minic-toolchain/actions/runs/38670658498) 均在 **Python C 源补丁脚本的精确文本匹配** 阶段失败，未产生任何性能数据；不是被测编译器错误。

| Linux TU | Boolean integer direct / transitive | 已加 owner+integer CALL 之后 integer direct / transitive |
| --- | ---: | ---: |
| `lib/crc32.o` | 10.90ms / **8944.75ms** | 11.61ms / **5024.07ms** |
| `net/core/dev.o` | 8.42ms / **3336.82ms** | 7.88ms / **2303.95ms** |
| `net/ipv4/tcp_input.o` | 7.28ms / **2077.56ms** | 7.44ms / **1485.26ms** |

**明确结论：剩余整数专门化耗时的 99% 以上都在 transitive，不在 initial/direct。** 仅此是单次插桩相位统计，不能以两臂 wall 时间差直接宣称产品级优化速度。

代码中，integer transitive 在每个 integer-specialized caller 克隆内遍历多个 CALL expression id，但旧实现未限制各 CALL 属于该克隆的原始源函数。因此新增独立 opt-in `apply-perf-integer-function-body-owner-filter-v1.py`：**复用此前已通过 first500 验证的 `minic_perf_program_build_expression_source_owners`**，仅当表达式有唯一明确原始 source owner、且 owner 与当前克隆 `caller.specialization_source` 不同时跳过；未知、共享和模糊 owner 保持原扫描，分配/验证失败无条件回退旧算法。顺序、候选匹配、clone 限额等逻辑不改；所有退出路径清理新分配的可选缓存。

[独立 A/B CI #38671271982](https://github.com/yituanxing/minic-toolchain/actions/runs/38671271982) 在同 3 份真实 SHA-frozen 输入上，用两臂 **都已开启 symbolic-owner+integer CALL**、仅候选增加 integer-owner 过滤，每臂热身一次加两次配对，强制逐字节 `.s` 一致。需按最终日志判定；如出现源码补丁失败或 `.s` 不一致，不能晋级 candidate，也不能声称速度收益。M0 [#38671271885](https://github.com/yituanxing/minic-toolchain/actions/runs/38671271885) 已通过。


## 34. PERF-029：三个已测有效优化**同时启用**的 500 文件汇编等价验收（2026-10-15）

[CI #38670218353](https://github.com/yituanxing/minic-toolchain/actions/runs/38670218353) `gcc-single SUCCESS`；冻结 Linux 6.6.143 RISC-V 500 份真实 Kbuild `.i` 的 manifest、每份 input SHA 都完成校验，候选在同一 Runtime+Boolean 上**同时**启用 `apply-perf-symbolic-function-body-owner-filter-v0.py`、`apply-perf-integer-call-expression-index-v1.py`、`apply-perf-parser-function-hash-v0.py`（其前置诊断 Parser lookup counters 不改变算法；全量候选尚非默认），保持 GNU .o 身份和 Kbuild .config 不变。对 500 文件分别执行原 Runtime Boolean 与联合候选的完整 `-S` 编译，4 路 worker，任何一侧非零、超时、汇编不同即 fail。

**实际日志完整：** `MINIC_FROZEN_DIFF_PROGRESS checked=50/500,...,500/500`；`MINIC_FROZEN_DIFF=PASS selected=500 identical=500 runtime_p1_not_proven=true`；`PERF_FIRST500=PASS selected=500 combined=true QEMU_p1_not_executed=true`。这次不是上一次只有 symbolic-owner 的 500，而是**三算法联合**通过 500/500 汇编严格字节等价；已具备向缓存恢复的 **7 路全 Linux Kbuild/链接/QEMU P0+P1** 递进的正确性证据。仍不得以编译输出等价推断全量 Linux 2064/2064、链接、QEMU shell/P1 已通过。任一工具链认证切换必须由独立该阶段 CI 数据做决定。

在 500 验收执行时，采用独立的旧缓存执行 PERF-028 的整数子阶段归因（99%+ 处于 transitive），随后提出专门的 integer FunctionBody owner 过滤候选；其 `.s` 等价与 P50 尚待 [CI #38671520608](https://github.com/yituanxing/minic-toolchain/actions/runs/38671520608) 最终验证。该候选**不包含在本次 500/500 已过的三算法集合**，必须单独资格筛查，不能借本次 500 给它认证。


## 35. PERF-030：整数 transitive 的源函数归属过滤真正产生大收益（2026-10-15）

[CI #38671761601](https://github.com/yituanxing/minic-toolchain/actions/runs/38671761601) `gcc-single SUCCESS`。原先 [#38671271982](https://github.com/yituanxing/minic-toolchain/actions/runs/38671271982) 和 [#38671520608](https://github.com/yituanxing/minic-toolchain/actions/runs/38671520608) 因 C 源生成器的错误处理路径文本锚点不正确失败：没有 A/B 数字，且并非候选生成不正确。修复为在 integer-specialization 具体函数范围内匹配 clone 创建失败 return，CI 确认 patch 构建及实际编译通过。

完整实验对比在两臂均启用 `Boolean+symbolic-owner+integer-CALL`，**仅候选增加 integer-transitive FunctionBody owner source filter**；缓存 SHA-frozen GNU-Kbuild 真实输入、每臂热身一次加两次交替配对、所有 `.s` 完全字节一致：

| 新优化对象 | 两臂共用原算法 P50 | 加 integer source owner 后 P50 | 局部加速 |
| --- | ---: | ---: | ---: |
| `lib/crc32.o` | 7636.481ms | 4947.350ms | **1.5435×** |
| `net/core/dev.o` | 9359.325ms | 8082.939ms | **1.1579×** |
| `net/ipv4/tcp_input.o` | 7274.831ms | 6318.515ms | **1.1514×** |

局部 3 owner P50 之和约 24.271→19.349s，节约 **4.922s（20.3% 用时）**。`crc32` 尤其明显，正对应真实阶段测出的整数 transitive 5.024s 热点，**不是瞎试**。实验状态 `3-real-owner-byte-equivalent / independent-BPF-cohort-needed`；因为当前仅测试过三份冻结输入，没有将此新算法纳入原已验的联合 first500，不应自动声称 QEMU P1 或整个 Linux 20% 提速。

**下一检查：** 单独在此前 BTF/verifier/filter 三份、完全相同的 Boolean+symbolic-owner+integer-CALL 基准上执行 integer owner 过滤，并检查汇编逐字节相等；然后将其与原三项索引联合执行 SHA-frozen first500，再决定是否加入七路 Linux 全量运行时认证。相反，如果独立 BPF 组出现退化或语义变化，先暂停进一步加码源码索引。


## 36. PERF-031：验收者角度的近期优化评估与四索引合并阶段检查（2026-10-15）

**现在值得保留的、已证实的性能路径：** `Boolean` 已经在 3 个 BPF/net 长尾上有 1.79～1.94× 的旧实测局部收益；`symbolic FunctionBody-owner` 在两个独立的三 TU 组上分别 1.24～1.42×；`integer CALL-only` 在 `crc32` 有明显额外改善（其他网络文件收益较小）；`Parser earliest-first function hash` 在 symbolic+integer 均开启后两个独立三 TU 组中均额外加速（1.20～1.44×）。**不是将这些单项倍率相乘**。前三个候选索引同时开启（symbolic/ integer CALL/ Parser）已完成 500/500 Linux Kbuild `.s` SHA 相同，CI #38670218353。新 `integer FunctionBody-owner` 相对 Boolean+symbolic+integer-CALL，在 crc32/dev/tcp_input 分别局部 **1.5435×、1.1579×、1.1514×**，CI #38671761601；这四个索引同时开启的 500 个真实输入仍单独待证。

**从工程代价看，尚需改进：** Integer-owner 新算法最初有数次 Python 文本 patch anchor（不是 MiniC 语义）导致浪费 runner 时间，不应把流水线排查失败当成优化失败，也不应继续在已经几毫秒的 direct initial pass 上迭代。后续优先源码区域限定定位 + CI 早失败，并停止在 P14 / Parser global hash 等已验证微小收益上反复调参。所有实验仍仅沿用已有 `gcc-single` Job、GNU gold objects、保存的 `.i`、paired-result JSON、M0 原子清单，不增加 workflow。

[四索引 first500](https://github.com/yituanxing/minic-toolchain/actions/runs/38672453023) 验收已在执行：Boolean baseline vs 同时开启 symbolic owner / integer CALL / Parser hash / integer owner 的实际候选，必须 500/500 两臂均编译成功、汇编完全逐字节一致才可推广；此前一次变更到 cache restore 条件造成 workflow validation 失败（#38672263032，0 jobs），现已复用经过验证的 `[linux-perf-integrated-first500]` cache selector + `[linux-perf-owner-extra]` source-only tag，M0 已 PASS，前项失效不能算作四索引编译失败。

为了避免继续针对过去已消失的热点做优化，已更新现有阶段追踪助手，可以在 **Boolean+symbolic owner+integer CALL+Parser hash+integer owner** 的实际新候选上同时输出 parser、core-set、codegen 和整数 direct/transitive / symbolic phase 的绝对耗时，**两臂均装同一计时插桩、实际汇编 SHA 一致**。用 3 份独立真实 Kbuild frozen crc32/dev/tcp_input 输入，与历史大优化前数据区分。该检查是一次阶段归因，不是性能倍数测试；只为确认下一步该优化哪个模块。


## 37. PERF-032：三项联合优化通过**完整 Linux 2064 C + 七路合并 + GNU 链接 + QEMU P1**（2026-10-15）

**决定性正确性验收已经成功！** [CI #38675862094](https://github.com/yituanxing/minic-toolchain/actions/runs/38675862094) `Linux Runtime GCC Baseline SUCCESS`。本轮候选是在现有经 P1 历史验证的 RISC-V Runtime/Crossblock 编译器上附加 **符号 FunctionBody-owner、整数 CALL-only expression index、Parser earliest-first function hash**；这些索引已先经同一 `.i`、500/500 完全一致验证。使用已有固定 GNU Kbuild `.config` 与 2064 C owner manifest，七个现有 shard matrix **7/7 SUCCESS**，receiver 恢复七片对象哈希、`GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`，再复用完全相同的 MiniC 2064 C objects（`COHORT_CANDIDATE_CACHE=HIT`、`COHORT_COMPILE=SKIPPED`）GNU 完整链接与 QEMU：

```text
COHORT_SCOPE=FULL_C_UNIVERSE objects=2064
COHORT_GOLDEN=PASS objects=2064
COHORT_GNU_PREFLIGHT_VOLATILE=EXACT_SEVEN_RECEIVER ...
COHORT_GNU_PREFLIGHT=PASS objects=2064
COHORT_CANDIDATE_CACHE=HIT objects=2064
COHORT_ABI=PASS objects=2064
COHORT_COMPILE=SKIPPED reason=exact-object-cache-hit objects=2064
COHORT_TRIAL name=full_all count=2064 verdict=PASS image=71647b27...
COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064
COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS
COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true
```

**边界：** 此次其他工具仍为固定 GNU assembler/linker/objcopy 基线，证明的是新 MiniC `.i → .s` 编译及 GNU Kbuild 联合语义 P1，而非 MiniPP/MiniAS/MiniLD/全原生工具链也在这次同时替换。`COHORT_P1=PASS syscalls=12` 含完整已注册 Python-era P1 runtime oracle；尚没有对**整个 Linux**做与原版 Runtime 的相同环境墙钟 paired 对照，不能把局部 1.2～1.4× 等同为 Linux 全量同倍数。完整 SHA-固定 Image artifact [#38675862094 的验证工件](https://github.com/yituanxing/minic-toolchain/actions/runs/38675862094/artifacts/11946487570)，Image SHA 以 run 的 `71647b27...` 固定。

**过程真实问题及修复（避免误报/重复工作）：** 首个七路 run #38674405151 在 MiniC 编译前因 `init/version.o`、`vmlinux.o` 的 GNU Kbuild 时间戳变化触发 preflight 拦截，绝不是 MiniC 功能错误。为七个 immutable GNU snapshot shard 分别建立仅三个 volatile target 的严密 scope；#38674726958 的七片编译/ABI **全部成功**，但 receiver 同样被 Kbuild 时间戳拦截。最后增加 `COHORT_GNU_PREFLIGHT_ALLOW_SEVEN_RECEIVER_VOLATILE`，严格只允许 `COHORT_ACTION=verify`、`FULL_C_UNIVERSE`、2064、固定 `.config` 和 full-crossblock。其他 GNU `.o` 全部保持 SHA 校验；preflight 后完整恢复可信原树。全部工具链优化与 GNU 对象缓存身份都 SHA 绑定，故最终只复用成功七片，不重新执行 2064 个 MiniC 编译来处理 CI 问题。

**此外发现新的全局物理输出热点（尚未归因）：** 本次完整 TU 对比日志 `COHORT_TU_BYTES_SUM gcc=66379160 minic=456484104 count=2064`，MiniC 编译对象总大小约是 GNU 的 **6.88×**。差异可能来自当前优化程度不同、指令数、对齐、符号/重定位、未删除 section 等；不是“证明浪费字节”，而是**值得新增 ELF section/relocation/symbol 体积分解审计**的最大系统级磁盘 IO/最终库归档吞吐疑点。优先使用已经保存的 exact `.o`（无需再编译），按 `readelf -SW` 和 `size` 分解 BTF / 大网络 / 小文件，再决定是否有代码生成器层级结构性问题。


## 38. PERF-033：真实 Linux 上 AST 1.5× 容量增长没有获得预期收益（2026-10-15）

[CI #38676131973](https://github.com/yituanxing/minic-toolchain/actions/runs/38676131973) `gcc-single SUCCESS`，复用缓存中 3 个真实 Linux 6.6.143 frozen Kbuild `.i`，比较默认 `MinicExpression/MinicFunction` 动态数组 **2× capacity 增长** 与仅改变增长系数为 **1.5×** 的 opt-in 候选。两臂每个文件各热身一次、2 次配对，三份 `.s` 字节全等；另单次独立 `/usr/bin/time -v` 读取峰值 RSS。

| Linux TU | 2× baseline P50 | 1.5× 候选 P50 | 加速（>1 好） | baseline RSS | 候选 RSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| `lib/crc32.o` | 23739.367ms | 23316.462ms | 1.0181× | 306512 KiB | 305900 KiB |
| `net/core/dev.o` | 17906.457ms | 18484.736ms | **0.9687×（退化）** | 506468 KiB | **511196 KiB（退化）** |
| `net/ipv4/tcp_input.o` | 13870.138ms | 13290.070ms | 1.0436× | 477672 KiB | **480592 KiB（退化）** |

**结论：保留原 2× 默认实现，拒绝推广 1.5× 容量倍率**。虽然某些容量阈值的理论空闲可能缩小，但 `realloc` 时复制 680B/Expression 的额外数据、内存分配器分桶以及峰值其它阶段决定了整体 RSS，本轮实际 dev/tcp 反而更大；三文件局部 P50 有正有负、收益不稳定。仅改变增长系数不能有效解决 AST 形态的 680B/Expression（仅 4–6% 是 CALL）和 6536B/Function 固有体积问题。下一步若投入大内存重构，优先研究 CALL arguments 定长 64×8B 的旁路 arena/稳定 ID、Function parameter_types 的小对象分离，并先做表达式/函数浅复制、释放、写入点全面审计，避免直接把 union 换成裸指针造成所有权错误。

这是在用户提出 **`.i`→Token→AST→Inline→Core→RV64 `.s` 全链路审计**后最先验证的数据结构类假设；我们已经证明“缩扩容倍率”没有稳定收益。不能为了已有工作而继续微调它。另独立已验证的三算法七路 2064 编译+GNU Link+QEMU P1（#38675862094）均 PASS，不能混淆为本轮 1.5× 的认证。


## 39. PERF-034：第四项 integer source-owner 与原三项联合 first500 通过（2026-10-15）

[CI #38672453023](https://github.com/yituanxing/minic-toolchain/actions/runs/38672453023) `gcc-single SUCCESS`，已完成固定 GNU Kbuild Linux 6.6.143 RISC-V `selected-tus.txt` 的 500 份输入 SHA 验证，并在 **同一候选** 同时启用 **symbolic FunctionBody owner、integer CALL index、Parser earliest-first function hash、integer FunctionBody owner** 四项索引。固定 Boolean Runtime 编译器作基准，严格要求两臂编译成功和 `.s` 逐字节 SHA 相同，实际日志进度 50→500 后显示 `MINIC_FROZEN_DIFF=PASS selected=500 identical=500 runtime_p1_not_proven=true` 及 `PERF_FIRST500=PASS selected=500 combined=true integer_owner=true`。因此第四项已经从 3 owner A/B 晋级为**四索引共同 first500/500 字节一致**，不能再把这次记录成“待验证”。

**认证边界：** [完整 Linux 7-shard / 2064/2064 C / GNU linker / QEMU 12 syscall P1 #38675862094](https://github.com/yituanxing/minic-toolchain/actions/runs/38675862094) 已通过的仍是**前三项**（symbolic owner、integer CALL、Parser hash），**不是本条的四项**。若要为 integer-owner 新增正式 Runtime 身份，须在已有的 7-shard cached 2064 owner 统一配置里追加第四项并重新签发 GNU link/QEMU P1；不能将三项的完整认证自动授予四项。

## 40. PERF-035：Boolean 已集成后的 Integer Transitive 归属过滤（2026-10-16）

**现象（当前源码与剖析）：** 当前正式 Runtime44+Boolean MiniC 的 Callgrind CPU profile [#38920186536](https://github.com/yituanxing/minic-toolchain/actions/runs/38920186536) 记录 `minic_specialize_inline_integer_calls` 在 `fs/ext4/fsync.o` 和 `kernel/bpf/btf.o` 分别占据 **10.98% / 13.55% 的独占指令**（不是 wall time）。其中 `minic_inline_specialization_argument_value` 在同一 Integer 阶段分别被调用 221,400 / 659,316 次。生产源码已有 CALL-only 索引（只遍历 CALL），但每个 clone 仍会扫描所有函数所属的 CALL，导致 `K * C` 工作量；不是 Boolean Domain Cache 已解决的查询。

**源码与假设：** 复用此前 `tools/ci/apply-perf-integer-function-body-owner-filter-v1.py` 的**已试验语义**，直接落在 `src/compiler/compiler.c`：使用现有 `minic_perf_program_build_expression_source_owners`，仅跳过能确证唯一归属**其他原始源函数**的 CALL；对于没有 owner、共享/歧义节点，以及分配或所有权验证失败，保留旧完整路径。CALL ID 的原始次序、克隆预算、first-match 变体匹配及失败处理语义不改变。只在此 integer transitive pass 生效，且新建所有权表只在本阶段使用；修复克隆限额/添加失败时释放本阶段临时表的出口。增加 opt-in `MINIC_INTEGER_OWNER_PERF`，记录候选扫描与已过滤 CALL 数。

**实测同一 Runner A/B：** 源码提交 [`381bfd4b`](https://github.com/yituanxing/minic-toolchain/commit/381bfd4b3209004168fea224a9675055ad6141f6)；固定 GNU Kbuild `.i` 输入，旧版为 `9bbe1b37` 的全部相同源代码及工具配置（仅 `compiler.c` 为旧算法），每臂 warm-up 1 次 + **4 次交错实测**；新旧 `minic-cc` SHA 分别为 `79357ad1e4f4...`、`5011f702eed1...`，输出 `.s` 的 SHA 全部一致：

| 冻结真实 Linux owner | 旧版 P50 | 新版 P50 | A/B 速度比 | 剔除其他 FunctionBody 的候选 CALL |
| --- | ---: | ---: | ---: | ---: |
| `fs/ext4/fsync.o` | 662.614ms | 487.929ms | **1.3580×** | 635,887 / 648,948 |
| `kernel/bpf/btf.o` | 2,529.899ms | 1,933.050ms | **1.3088×** | 2,075,478 / 2,133,091 |
| **两 TU P50 之和** | **3,192.512ms** | **2,420.980ms** | **1.3187×** | **2,711,365 / 2,782,039** |

**可靠性证据：** [A/B CI #38925489139](https://github.com/yituanxing/minic-toolchain/actions/runs/38925489139) **SUCCESS**，`MINIC_SINGLE_FACTOR_AB=PASS trial=integer-owner owners=2 assembly_identical=2/2`，`MINIC_INTEGER_OWNER_FILTER ready=1 fallback=0`，独立统计 run 的 `.s` SHA 再次与两臂完全相同。[Focused T1 #38925489418](https://github.com/yituanxing/minic-toolchain/actions/runs/38925489418) **SUCCESS**。原先 [#38671761601](https://github.com/yituanxing/minic-toolchain/actions/runs/38671761601) 在不同的早期工具配置上已为三个其他 owner 取得局部收益，[#38672453023](https://github.com/yituanxing/minic-toolchain/actions/runs/38672453023) 曾为组合四索引完成 500/500 字节一致，但它们**不代替此新正式源码身份**的资格测试。

**阶段状态：`verified-local`**。这轮测试不能推出 Linux 全量 1.3187×；也还未给提交 `381bfd4b` 完成 2064/2064、GNU link 和 QEMU P1 认证。下一步仅复用**现有 perf500 job** 的 first500 SHA-frozen paired driver，确保 500/500 字节一致与全体时间分布；有全体正收益再用既有七路 `gcc-shards`、receiver QEMU 做正式认证。坚决不重复跑耗时的 Callgrind，也不新建 workflow/job。

## 41. PERF-036：Integer owner 当前正式代码的七路 Linux / QEMU P1 完整资格（2026-10-16）

补上 PERF-035 的完整资格：生产 `src/compiler/compiler.c` 的 `integer_expression_owners` 索引，以及此前 Parser 三处整体拷贝优化，已在 [CI #38927659471](https://github.com/yituanxing/minic-toolchain/actions/runs/38927659471) 取得 **七片 7/7 SUCCESS、Linux 2064/2064 C、GNU 链接、QEMU P1 12 个 syscall 全 PASS**。Run 的正式 MiniC 完整配置是 GNU 预处理、MiniC 编译、GNU 汇编和 GNU 链接，**不能借它声称全 Mini 工具链同时替换**。

- `GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`；
- receiver 从七片 SHA 锁定的 `.o` 直接恢复，`COHORT_COMPILE=SKIPPED reason=exact-object-cache-hit objects=2064`；
- `COHORT_TRIAL name=full_all_verified count=2064 verdict=PASS image=a2f88ce35f5abb15066dc5a73b05cee3de5725ce857b93283ba18286014ac6c4`；
- `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`；
- `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`。

七片 `MINIC_INTEGER_OWNER_COUNTS=PASS` 合计记录 **933,704,798** 次 transitive CALL 候选迭代，其中 **906,240,903（97.06%）** 属于可明确归为其他源函数的表达式，已由安全过滤器跳过；剩余 **27,463,895** 次仍继续执行后续判断。零 owner-index fallback。此计数是循环迭代次数，不是 CPU 耗时或节省的 DRAM 字节。正确性已完成，完整 Linux 的同比较环境墙钟**仍未**做一轮同 Runner baseline/candidate，因此 500 文件的 1.49261× **不能冒充**七路 Linux 端到端加速。

第一次七路 [#38926982886](https://github.com/yituanxing/minic-toolchain/actions/runs/38926982886) 是新统计 Kbuild Wrapper 的字符串插桩把 `asm_args` 之后 shell 代码截断，`bash -n` 在所有 shard 报意外 EOF；无对象编译结果。重新从已知正确版本构建 Wrapper，并保留 mode 100755，修正后没有追加新 Workflow，仍复用七路现有 Runner 和 receiver。

## 42. PERF-037：新瓶颈已由实测转移至 Core Lowering，而非继续整数特化（2026-10-16）

**先测后改，未盲选：** 优化后 MiniC 的现有阶段 trace（没有 Callgrind）在七份冻结 GNU Kbuild `.i` 上 [CI #38929306497](https://github.com/yituanxing/minic-toolchain/actions/runs/38929306497) **SUCCESS**；测试范围为 `fs/ext4/fsync.o`、`kernel/bpf/btf.o`、`kernel/events/core.o`、`mm/memcontrol.o`、`mm/hugetlb.o`、`mm/vmscan.o` 和 `fs/ext4/super.o`。每个文件的阶段边界由 `MINIC_BOOTSTRAP_TRACE` 与原有 `tools/ci/linux-perf-phase-trace-v1.py` 获取，完整汇编严格相等。下面是更细一级的低成本 C 白盒计数 [CI #38930177529](https://github.com/yituanxing/minic-toolchain/actions/runs/38930177529) **SUCCESS** 产生的同批七文件阶段耗时之和（独立重复诊断，会有正常计时波动）：

| 阶段 | 七个样本合计实测 | 是否属于当前应优先追踪的时间 |
|---|---:|---|
| **Core Function Set（含每函数 Lowering 与 Lower 后验证）** | **4,925.616 ms** | **第一优先级** |
| Parser | 1,876.555 ms | 第二梯队，Parser 复制已单独局部优化 |
| Symbolic Specialization | 1,029.222 ms | 第二梯队 |
| Boolean-range Specialization | 756.552 ms | 第二梯队 |
| Integer Specialization（已优化） | 352.351 ms | 暂不继续打磨 |
| Core-validate 独立阶段 | **52.528 ms** | **当前不值得优先微调** |
| Codegen | 377.597 ms | 后续观察 |

`core-set` 不是整个编译器耗时，也不能把此 4.93s 说成某一个内部函数独占耗时。为了进一步判别实际高频扫描，新增**只在临时 CI checkout 生效**的 `tools/ci/apply-perf-core-lower-hotspot-census-v1.py`，记录了这些循环的调用次数和理论最大遍历量，并在每个真实 TU 上额外编译一次证明 `.s` SHA 与阶段基准**完全相同（7/7）**，没有改生产 C：

| 白盒行为 | 七 TU 累计值 | 正确解释 |
|---|---:|---|
| `minic_core_lower_function` 局部语句映射初始化 | **5,854 次** | 实际调用计数 |
| 每次按整个 Program 分配并赋 INVALID 的 `statement_blocks` | **294,098,527 表项** | 实际初始化次数，按 4B 表项约 **1.18GB 的累计写入量** |
| `core_block_contains_reentry_label` | **2,942 次** | 其理论最多遍历 **152,476,168 statement + 532,282,297 expression** |
| `core_unreachable_statement_has_external_reentry` | **524 次** | 理论最多 **26,728,581 statement** |
| `core_unreferenced_internal_loop_label` | **690 次** | 理论最多 **35,485,296 statement + 123,340,986 expression** |
| `core_switch_label_has_function_reentry` | 0 次 | 本批七样本未触发，不应先优化 |

这里所谓 8.70 亿次 reentry 候选扫描，是源码枚举上下界的**潜在访问数**，含早退出及条件过滤；既不是实测 retired instructions，也不是实际 DRAM 流量。真正的相对节时只认等输入、同 Runner 的 A/B。

**下一单项候选：优先 `core_block_contains_reentry_label` 与规范化 loop-label 的程序级不可变 `goto/asm-goto/&&label` 源引用索引。** 目前每次条件分支重入证明都在不同 FunctionBody clone 上重新读整 TU 的 statement/expression，索引必须保持「外部进入」语义（root 子树内部 goto 不能误算外部引用）、switch case/default、computed goto、asm-goto label、任意不明确目标全部按保守路径回退，不得删除运行时安全检查。先计索引构建时间与命中次数，再在已冻结这七个 owner 的典型 2～3 文件上执行原算法 vs 单因素 A/B，要求字节一致，再决定扩大至 first500 及七路 P1。**备选**是一个安全的、按需清理的 per-function statement-block map，来消除 `F_L×S` 初始化；不能简单删除 `MINIC_CORE_BLOCK_INVALID` 初始化，否则会把未初始化块当已绑定 goto 目标。旧 Parser hash、整数 CALL/source-owner、Boolean Domain cache 和 Codegen 既有 slot reuse 都已在正式代码，**不要重新造这些优化**。

## 43. PERF-038：Core Lowering 只读目标反向索引（2026-10-17）

**选择依据：** 7 份真实 Linux .i 的轻量阶段归因 [#38930177529](https://github.com/yituanxing/minic-toolchain/actions/runs/38930177529)：Core Function Set 合计 4,925.616ms；core_block_contains_reentry_label 调用 2,942 次，理论全 TU 重复扫描上界 152,476,168 Statement + 532,282,297 Expression；循环标签重入 690 次、潜在上界 35,485,296 Statement + 123,340,986 Expression。这里的遍历上界不是实测 CPU 指令、耗时或 DRAM 流量。

**生产代码：** [32663329](https://github.com/yituanxing/minic-toolchain/commit/32663329d6035dbb6fc9b4f5ab3d6aeffd10e793)；仅改 core_lower.c/h、compiler.c 和 native SHA 身份。minic_prepare_core_function_set() 生命周期内显式 begin/end，一次读 Statement 收集 goto、asm goto 的 target→incoming source 链，一次读 Expression 收集 label-address 标记，校验及分配全部成功后才发布索引。失败、OOM 或单独调用 Core Lower 保留旧完整扫描。case/default、成员子树内部 goto、外部重入、计算 goto 地址标签、规范化 WHILE 源位置唯一性均保持旧保守语义。pass 后释放索引，不引入跨 TU 缓存或新编译器框架。可选 MINIC_CORE_REENTRY_INDEX_PERF 提供 indexed_if/loop 次数。

**五 Linux TU 同 Runner A/B**：[CI #38992107850](https://github.com/yituanxing/minic-toolchain/actions/runs/38992107850) SUCCESS；旧源码基准 3bcb2594，新源码 32663329；每编译器先预热一次，再交错进行 4 次真实 .i→.s；五份都字节级 SHA256 完全一致，候选额外诊断编译的 .s SHA 再次一致：

| Linux TU | 旧版 P50 | 新版 P50 | 加速比 |
| --- | ---: | ---: | ---: |
| fs/ext4/fsync.o | 508.006ms | 506.824ms | 1.0023× |
| kernel/bpf/btf.o | 1939.764ms | 1464.877ms | 1.3242× |
| kernel/events/core.o | 2834.657ms | 1860.925ms | 1.5233× |
| mm/memcontrol.o | 2434.701ms | 1741.226ms | 1.3983× |
| fs/ext4/super.o | 1389.997ms | 1122.534ms | 1.2383× |
| **五文件 P50 之和** | **9107.124ms** | **6696.386ms** | **1.3600×** |

[Focused T1 #38992107892](https://github.com/yituanxing/minic-toolchain/actions/runs/38992107892) SUCCESS，原有 8/8 检查。

**500 文件同一索引候选：** [CI #38992360179](https://github.com/yituanxing/minic-toolchain/actions/runs/38992360179) SUCCESS；复用原 perf500/first500 缓存和既有配对差分脚本，没有新 workflow/job。500/500 .s 完全一致，balanced-order=true；旧版累计 384418.072ms，索引版 348183.509ms，**1.10407×、节省 36234.563ms、耗时下降约 9.43%**。单轮配对有 97/500 个对象相对退化，应记录为单次噪声及可能的索引固定开销，不能假定所有文件都加速。热点大收益包括 kernel/events/core.o、kernel/bpf/verifier.o、mm/memcontrol.o。五热点合计 1.36× 不可当作全体 500 的收益。

**完整验收边界：** 当前为 verified-frozen500，尚未对此正式代码身份完成 2064/2064 + GNU Link + QEMU P1。本次仅沿现有七 Runner Linux CI 升级资格，不把上一轮 Integer source-owner 的完整内核认证误当成这轮认证。完整验收编译链仍是 GNU PP → MiniC → GNU AS/LD，不能说全 Mini 工具链已共同通过。

## 44. PERF-039：Boolean Transitive CALL-only + source owner（2026-10-17）

**根因：** Integer/Symbolic 已使用 CALL-only 和 FunctionBody 来源索引，但 Boolean Transitive 仍对每个带布尔 range 事实的 caller clone 扫描整个 TU 的 expression_count。现在复用已认证的 minic_perf_program_build_expression_source_owners，只有确证唯一且属于其他 canonical source FunctionBody 的 CALL 才跳过；未知 owner、共享 owner 和分配/验证失败走旧路径。CALL-only 索引保持原 ExpressionId 升序，首次真正出现有范围事实的 caller 才构建索引，异常或表达式增长保留旧完整扫描。变体匹配与克隆预算完全不变。支持 MINIC_BOOLEAN_TRANSITIVE_PERF 选通诊断。

**上一负面候选撤回：** Core reentry lazy #38994826749，500/500 汇编一致，旧版 439568.899ms、lazy 443102.585ms，速度 0.99203x，耗时 +0.80%。恢复经过 #38992779314 完整 2064/QEMU 验收的 eager Core Lowering，保证新的 Boolean 优化对比是一因子变更。

**五个真实 Linux 样本：** [CI #38995338704](https://github.com/yituanxing/minic-toolchain/actions/runs/38995338704) SUCCESS，基准 eager Core commit b45298017，对每输入 old/new 各排除 warmup 后交错计时四次，5/5 汇编 SHA256 相同，诊断 .s 再次核对。五对象 P50 合计 6960.675→6582.533ms，加速 1.05745x；fsync 1.0602x、ext4 super 1.0300x、bpf verifier 1.0600x、events core 1.0506x、memcontrol 1.0802x。诊断在五个文件合计访问 CALL 候选 1,026,411 次，排除另一个 FunctionBody 的 1,002,807 次（约 97.70%）；这是逻辑候选数，不是 retired instruction。

**推广 first500：** [CI #38995564349](https://github.com/yituanxing/minic-toolchain/actions/runs/38995564349) SUCCESS，冻结 500 输入全部汇编字节相同，balanced_order=true，baseline 382579.154ms，candidate 372710.334ms，节省 9868.820ms，速度 1.02648x，耗时 -2.58%。单轮 500 逐文件有 108 个对象测得变慢（fs/nfs/client、mm/slub、mm/hugetlb 等），需要通过重复配对 wall/user/sys 判别固定成本与噪声，不能把单次观测说成持久退化。

**目前状态：** verified-frozen500，不是 Runtime-certified。Focused T1 #38995262829 SUCCESS。正式源码 commit 88029a72。下一步现有 7 Runner 做 2064 MiniC GNU link QEMU P1，并单独复核大回退样本；没有充分收益后不继续在 Boolean 上微调。
