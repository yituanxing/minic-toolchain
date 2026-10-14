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
