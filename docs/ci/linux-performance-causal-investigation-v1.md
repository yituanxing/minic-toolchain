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
