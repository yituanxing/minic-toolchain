# MiniC 真实 Linux 编译耗时回归归因与准入规则（2026-10-17）

## 从 GCC、LLVM 借鉴什么

- GCC -ftime-report/-ftime-report-details 将编译耗时拆分到优化 pass 与其 infrastructure，而不是只看全局耗时。
- Clang -ftime-trace 生成逐阶段时间线；LLVM CTMark/test-suite 对代表性大型项目追踪 compile_time，并提供跨版本比较工具。
- LLVM CTMark 推荐单线程基准以降低资源竞争；LLVM Benchmarking 指南要求重复、控制噪声，并避免测量偏差。
- 结论并非“所有输入都能提速”；编译器优化必须同时管理正确性、统计可信度和不同 TU 的成本模型。

## 这轮 Core reentry 的可复核事实

- 固定原始 GNU Linux 6.6.143 Kbuild 前处理输入；旧版 SHA 3bcb2594，索引版 Core 源码 32663329。正式源码对应后续验收提交 5e25416。
- 500 个文件逐个 old/new 单轮交替验证：500/500 汇编 SHA 一致，旧版累计 384418.072ms，新版 348183.509ms，加速 1.10407 倍，节省 36234.563ms。
- 403 文件单轮观察为加速，97 文件单轮观察为回退；这 97 只是单次观测，不能当作已复现回归。两个实验臂作为先跑对象各 250 个，分别观察到 49、48 次回退。
- 97 个单轮回退的累计额外耗时约 1584.979ms，403 个加速文件合计节省约 37819.542ms；这里是一次测量的算术分解，不能当成置信区间。
- 96 个旧版运行超 1000ms 的文件，在这次单轮配对中 96/96 都更快；旧版 200~500ms 区间 84 个中 37 个更慢；最小的 4~14ms TU 百分比变动明显受启动/调度成本影响。
- 回退最大绝对值：fs/nfs/client.o +138.667ms (+14.82%)、kernel/time/sched_clock.o +53.330ms (+16.49%)、kernel/bpf/prog_iter.o +53.455ms (+5.93%)。不能仅根据该单次数据断定是算法固定开销。

## 源码层面的待验证解释

1. 固定索引成本：src/compiler/compiler.c 进入 Core Function Set 时无条件调用 minic_core_lower_reentry_index_begin；src/core/core_lower.c begin 会扫全 TU 的 Statement 和 Expression，分配 targets 的入边链和 addressed 数组，即使 indexed_if/loop 查询次数均为零。对于 TU 较小或相关查询较少的场景，建索引成本可能超过省下的查询扫描。
2. 有收益的条件：当 discarded-if 与 normalized-while-label 查询次数高、全 TU 特别大时，一次建索引在多个 Core FunctionBody/clone 之间复用，摊销效果更好，理论上节省约多次 O(Q×(S+E)) 扫描。
3. 测量噪声：四工作进程的整体 first500 配对不是每 TU 多轮独占测量；GHz 频率、CPU 抢占、文件系统缓存、用户态/内核态竞争、冷热启动都会让单 TU wall_ms 变动。两种 first 顺序的回退 49/48 很接近，支持未见明显固定“谁先运行”偏差，但不能证明无噪声。
4. 被改变的不是代码生成算法或 ABI；500/500 汇编一致。候选额外准备索引也可能改变宿主进程的分配模式、cache locality、代码布局，需用重复 CPU clocks 和 index hit counts 判断。

## 根因闭环流程

1. 原始 A/B：固定两个 minic-cc SHA、完全相同 Linux .i SHA，并对 .s 作字节哈希比对；记录全体 P50 分布、速度差、绝对回退和尺寸分桶，不能只看总体和最高赢家。
2. 小样本复核：保留原 Baseline 与 Candidate 源码，选原单轮回退前 10、tiny TU 2、有效加速 3、平稳 1 共 16 个，复用同一个 perf500 Runner；每 TU 两组各 4 轮交错测试，即旧/新各 8 次 + 排除预热。
3. 本轮新采集：在现有 linux-perf-pinned-single-paired-ab-v1.py 中扩展 Linux resource.RUSAGE_CHILDREN，逐编译同时记录 wall_ms、user_ms、sys_ms，配合 old/new 的中位数与 MAD；区分 CPU 真实工作增量和墙钟调度噪声。
4. 查询解释：额外使用同一生产 Candidate 的既有 MINIC_CORE_REENTRY_INDEX_PERF 统计 ready、incoming、indexed_if、indexed_loop、statements、expressions。要求诊断 run 的 .s SHA 与 A/B 一致。
5. 报告分类：与中位耗时相比，若新旧 wall 与 user CPU 都持续增加且超出明确噪声门槛，标记 suspect；若只涨 wall 或重新测量已消失，标记 mixed_or_noisy。分类只是工程风险提示，不是统计显著性检验。
6. 处理动作：对真实固定索引开销，优先惰性建立索引、合理保守门槛、低成本预筛，而不是简单牺牲大文件的显著收益。若已证明完全无查询，可先评估跳过索引的正确性与全体净收益；任何策略都必须追加两臂 SHA 和真实 Linux Runtime P1 检查。
7. 后续优化的每一步都保留单 TU 回退表。发现同一类文件持续变慢，应定位源代码调用规模和新增代价；即使全局提升也不得以总体平均数掩盖确定性回退。

## CI 结果索引

- Core index frozen500: https://github.com/yituanxing/minic-toolchain/actions/runs/38992360179
- Core index focused: https://github.com/yituanxing/minic-toolchain/actions/runs/38992107892
- Core index seven-shard Linux runtime: https://github.com/yituanxing/minic-toolchain/actions/runs/38992779314（记录此时 QEMU receiver 仍在执行，不冒充通过）
- 新的 16 TU 8+8 次复核运行依靠既有 perf500 job 的 [minic-core-regression-triage] marker；完成后把各文件重复测量与统计写回本节。

## 外部参考

- GCC developer options: https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html
- Clang compile trace: https://clang.llvm.org/docs/ClangCommandLineReference.html
- LLVM test-suite and CTMark: https://llvm.org/docs/TestSuiteGuide.html
- LLVM benchmarking: https://llvm.org/docs/Benchmarking.html
