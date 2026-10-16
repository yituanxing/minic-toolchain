# MiniC 轻量多前端/多目标方向与自举前收敛计划

**记录时间**：2026-10-16  
**状态**：架构意图与清理清单；不是实施新后端或立即重写 Core 的授权。  
**记录基线**：`main@24b0247401d1f973c4aa4f3495d7298cc7dd4cc1`；Linux 全链路 [Actions #38870185276](https://github.com/yituanxing/minic-toolchain/actions/runs/38870185276) 成功，工具链聚焦测试 [#38870185297](https://github.com/yituanxing/minic-toolchain/actions/runs/38870185297) 成功。此处所述验收只适用于该 SHA，不自动证明未来改动。

## 1. 项目定位与优先级

MiniC 是 **教学/研究友好、真实可用、能够自举、可逐步扩展** 的 C11 编写的编译器工具链，不以复制 GCC/LLVM 的工业功能覆盖率为目标。

优先级：**正确性与可复现性 > 日常迭代速度 > 代码可理解性和职责清晰 > 有实际消费者的扩展能力 > 未经需求验证的框架完整性**。可读性与速度并非对立：主要算法、输入输出和所有权应独立可观察；热点优化必须有同输入、同环境 A/B 测量。

严格区分：
- **长期方向**：多语言前端、共享 Core IR、轻量通用优化、目标可组合配置、共享后端框架、RV32/RV64、ABI、ISA 扩展、软浮点运行时、调试信息链路，以及必要时的轻量 Machine IR。
- **近期执行**：只整理当前单前端、RV64 正式生产路径中 **已证明无用、重复或存在边界错误** 的部分；保持现有 Linux 正确性和迭代速度；随后重新认证 **当前代码** 的自举。
- **当前不做**：凭想象拆 `core_lower.c` / `core_codegen.c`，为了目录整齐建立抽象层，新增第二前端/新后端，提前实现完整 Machine IR/PassManager/TableGen/动态插件系统，调整 Linux CI 工作流数量，移除历史诊断/差分脚本以求目录好看。

「容易教学」的验收标准不是代码行数少，而是能够从小程序独立跟踪每一步：Token、语言 AST/Sema、Core IR、优化前后、（未来的）机器表示、汇编、ELF，以及每一步的输入/输出、不变量和耗时。新增算法不应要求读者理解整个编译器才能掌握。

## 2. 未来职责边界——先记下来，不立即实现

```text
                         Driver / Build orchestration
                                     |
            +------------------------+-------------------------+
            |                                                  |
    Frontend A (C)                                    Frontend B (future)
    own Lexer/Parser/AST/Sema                         own AST/Sema
            |                                                  |
            +------------------------+-------------------------+
                                     |
                         Language -> Core lowering
                                     |
                           Core IR + verifier
                                     |
                        Small common IR passes
                                     |
                     Backend / Target legalization
                      + ABI-aware call lowering
                                     |
                    [optional shared Machine IR]
                                     |
                    [machine passes where needed]
                                     |
                       target emission (.s/.o)
```

- **前端统一点是 Core IR**，不是统一 AST。各语言独自定义语法、类型合法性和求值规则。目标的 DataLayout/基本数据模型可通过只读查询参与前端语义判定。
- **Core** 保留目标中立的执行语义、显式内存副作用、CFG、调用与已解析的类型/布局事实。长期不应要求非 C 前端构造 `MinicC0Program` 或 `MinicType`。跨基本块值传递/SSA 是长期演化主题，非本轮变更。
- **优化** 按语义拥有者归位：C 常量表达式合法性属于 C 前端；通用数据流、CFG、DCE 属于 Core pass；栈槽/寄存器生命周期属于机器层。不可把保证语义的 lowering 和可选性能优化不加区分地搬到 Pass。公共优化可查询目标 cost model，但不可直接生成目标指令。
- **Target** 要按 Architecture、XLEN、ISA feature set、DataLayout、Calling Convention/ABI、Object/Relocation、OS/Runtime 拆清职责并验证组合合法性。RV32/RV64 复用 RISC-V 指令、寄存器规则和算法的共同部分，而不是复制整套后端；但 **XLEN != 指针宽度 != ABI**。配置化不代表任意组合均有效。
- **ABI** 在调用、参数、返回值和栈帧相关 lowering 阶段参与，不是发射之后的选项。
- **Runtime libcalls** 由合法化按目标能力选择（例如无 F/D 扩展的软件浮点、超宽整数操作）；需保证对应库和 ABI 真正可用，特别是 freestanding/内核环境，不得偷偷注入外部依赖。
- **调试信息** 从前端保留源码位置/变量身份，跨 IR/机器变换传递，最终 DWARF/其他格式由输出层编码；不能只在 Emitter 添加调试开关。
- **Machine IR 是候选边界**：等现有 RV64 backend 确实出现与发射耦合的、可独立验证的寄存器/栈/指令操作需求时再引入；可以共享 Generic/Target MIR 数据结构，避免三四套不断互转的 IR。

参考本仓库已有设计文档：`docs/architecture/principles.md`、`core-ir-multi-frontend-roadmap.md`、`representation-seams-and-deferred-ir.md`、`toolchain-component-boundaries.md`。以前写的「shadow-only」「first500 未完成」等文字属历史里程碑，不能代替当前 main 的 Core-only 和完整 Linux 验收事实。

## 3. 当前源码：已核实的结构与债务

| 位置 | 实际情况 | 近期处置 |
|---|---|---|
| `src/compiler/compiler.c` | 负责 Parser→Parsed/Normalized AST 验证→inline specialization→Core lower/verify→layout→RV64 emit | 保留流程；把阶段计时和每次状态作为可观察事实，检查重复执行是否**必要**；Parsed/Normalized 两次验证不能直接当作冗余删除 |
| `src/frontend/` | C 语义/Parser 已拆成多个独立文件；AST 使用 Program 级数组和 ID | 保留；不要复制另一套 AST；按真实峰值内存评估大 union / 容量开销 |
| `src/core/core_ir.h` | 仍依赖 `frontend/type.h`、`MinicType`；Core verifier 按 block 清空 value available 状态 | 记录为未来多前端障碍，不在自举前为理论扩展重做 Core 类型/SSA |
| `src/core/core_lower.c` | 约 1.6 万行，混合 lowering、局部事实与 CFG 处理；`core_lower_bool.c` 已提取零值布尔证明且通过全量认证 | 不重复拆大文件；只有能独立证明正确性、性能并明确 ownership 的成块逻辑才移动 |
| `src/target/riscv64/core_codegen.c` | 约 7000 行，已有值/对象槽复用和生产者索引；尚含布局、生成与机器策略 | 不重新发明已有优化；若重复 layout 查询确为热点，先计数再小范围改 |
| `src/target/target_info.c`、`data_layout.*`、`riscv64/abi.*` | 默认目标与前端 AST 关联，且当前以 RV64 为主 | 保留，等 RV32/第二前端有真实消费者再消除相关耦合 |
| `tools/ci/apply-*.py` 等 | 保留大量历史诊断、差分和回放代码；不能仅按文件名断言生产仍调用它们 | 先据入口、脚本调用图与验收结果分类；不删除未证明无用的资产 |

注意：最近全量性能与结构审计已有 `docs/ci/minic-i-to-s-whole-path-complexity-audit-2026-10-15.md`、`minic-full-linux-2064-performance-evidence-2026-10-15.md` 和 `native-source-convergence-audit-2026-10-16.md`。清理前先复用它们，勿再凭经验把已经修复的热点当作新问题。

### 一个已证实、影响下一次自举的清单漂移

`Makefile` 的当前编译器 `MINIC_SOURCES` 与 `tests/compiler/bootstrap/b0-sources.txt` 不一致：

- 当前 compiler 编译项是 `src/core/core_lower_bool.c` **和** `tools/minic-cc/main.c`；
- 历史 B0/B1 清单缺少 `src/core/core_lower_bool.c`，且使用 `tools/minic/main.c`（现在是统一 Driver，而不是 `minic-cc` 的编译器入口）；
- 当前清单 46 项、Makefile 编译器 47 项；如果重建完整当前编译器，自举清单需要按 **目标可执行程序的归属** 核对，并增加独立 Lowering 源文件；
- 历史自举工作流位于 `.github/workflows-disabled/`，旧的 B1 stage2 仍有 `selected == 46` 等写死假设；`make bootstrap` 现在也只是提示文本，不能误当作执行了 Stage0→Stage2。

**处置**：自举前以当前 `minic-cc` 为目标同步 *清单 + stage0/1/2 链接入口 + 计数/验证条件*；先复用、最小修正旧测试，不为这个问题新建一套独立工作流。不要只改清单一行却让旧工作流静默失败；这里的旧 B1 曾经通过，但它认证的是**当时**的源码，不是 2026-10-16 当前主线。

## 4. 性能与空间：先用事实排队

### 已认证、不可随意更改的基线

- 最新源码 `24b02474`：[七路真实 Linux + QEMU #38870185276](https://github.com/yituanxing/minic-toolchain/actions/runs/38870185276)：**2064/2064 native C objects、7/7 shard、QEMU P1 12/12、Python-era oracle PASS**。MiniAS 和 MiniAR/MiniLD/MiniObjcopy 真实集成通过。该验收是 correctness gate，不是优化前后配对速度证明。
- 各 GitHub job 总耗时（包含环境和 artifact，约）：**12:39、15:06、13:34、15:24、13:07、7:43、13:47**（shard 0～6）；并行最慢分片 15:24。仅按文件数平分的代价很明显。
- 这次每个 shard 的 MiniC 子进程调用 wall 合计分别约 **9:24、9:54、8:40、10:27、9:14、4:44、9:44**；所有 shard 的 MiniC 调用总和约 **62:07**，绝不能当成 7 路并行墙钟。
- 历史有同输入 2064 `.i → .s` 配对优化证据 **18,606.071 s → 14,261.185 s (1.3047×)**，不是这轮 MiniPP+MiniC+MiniAS 的同比数据；新修改要重新配对，不能复用该倍数当成当前收益。

### 候选顺序（不是已证明的优化）

1. **最省时间的反馈循环**：复用 `native-performance.json`、现存 `linux-perf-phase-trace-v1.py` 和分片 wall 数据，识别当前最慢的 owner 和阶段。可用已有 `linux-perf-time-balanced-shard-plan-v1.py` 审计负载均衡；先只改本地/显式测试排片配置，验证样本一致，不能破坏原有 Linux owner 集合。
2. **内存**：`MinicExpression` 的内联 64 参数数组放大所有表达式节点；Parser switch stack 常驻数据也是候选。先记录结构体大小、实际调用参数分布、容量利用率和 RSS。已探索过的 AST 2×→1.5× 容量策略不能在未复查负收益之前重新重复实验。改成堆指针会改变所有权与随机访问成本，不能作为默认方案。
3. **时间**：`core_lower_bool.c` 的 whole-source 扫描、inline specialization、Parser 查找、Core 函数/值索引、RV64 codegen 重复 layout 查询，是已有审计中待排序的候选。已有函数索引/源归属过滤/槽复用不能再次以「新增优化」名义重做。必须先有实际阶段占比与调用次数。
4. **清理**：删除旧分支/测试/脚本/所谓无用代码之前，必须证明它不在当前编译链接、诊断回放或保留验证路径上；如果只是过时文档，**标注历史**优先于覆盖原始验收记录。

每个性能变更单独记账：`基线 SHA / 输入 SHA / 旧版 wall、RSS / 新版 wall、RSS / 汇编或语义差分 / 优化机制 / 是否收益明显 / 全量 Linux 结果`。局部性能大幅提升但全量基本不变，优先换瓶颈。严禁为跑绿直接绕过编译器源码 SHA/缓存身份校验。

## 5. 近期变更原则与实际推进顺序

1. **先保存架构意图与审计事实**（本文），避免把「未来多前端/多后端」误执行成当前的目录拆迁。
2. **无行为清理**：只修复已经核实的归属、清单和文档漂移；仅删除有实际调用图证据的死代码；不得影响现有核心编译器源码的缓存身份而不重新认证。
3. **目标明确的极小源码改动**：每次最多处理一个重复规则或一个实测热点，先源码/汇编/对象的最窄验证，再 T1 聚焦，最后按代码修改范围重跑 2064 native owner 与 QEMU P1。行为变化与纯重构不得混为一次提交。
4. **性能回归门槛**：新旧相同环境/输入至少成对比较 wall 与 RSS；总体无意义、明显变慢或增加教学复杂度则回退。省去无效扫描很有价值，但不可为省一次扫描损害 correctness。
5. **自举重新认证**：参照 `docs/milestones/compiler-v1-frozen.md` 中历史通过的 Stage0→Stage1→Stage2 fixed point，恢复现有 B1 方法，并对当下的 `minic-cc` 正式源码与真实依赖清单重做。Stage0 可由宿主 GCC 构建交叉编译器；Stage1 在 qemu-riscv64 下重新编译自己的全部 C 翻译单元；Stage2 重复编译并检查汇编/可加载镜像固定点，再用 Stage2 进行已有真实程序与 Linux 代表性 owner 验证。分清「当前编译器自举」与「完全不依赖 GNU 任意工具」是两个独立目标。
6. **才考虑新特性**：取得当前源码可复现、可测速的自举基线后，再以 RV32 或一个极小第二前端作接口压力测试；不因长期架构方向提前动手造新后端。

### 完成定义

- 有一条**可复用而非描述性**的当前源码自举执行路径；清单与实际链接输入一致；
- 当前 Linux 6.6.143 2064 native C + QEMU P1 12/12 不回归；
- 不删历史已验证算法、不创建新的 CI job/workflow、不假冒已验证的性能提升；
- 每一个保留的清理改动都有明确的减少重复/复杂度/时间/空间理由；
- 继续从真实单文件最快失败反馈推进，避免每次验证都重新走全部重活。

## 6. 需要下轮有证据才执行的事项

- 对 `Makefile` 与 B0/B1 的准确入口、静态链接输入、环境和 46→当前数量变更制定最小补丁；**旧脚本被禁用且有硬编码，不能只增添 `core_lower_bool.c` 就宣称自举已修复**。
- 将官方当前代码路径与历史约束写成简明索引，保留原始里程碑和失败日志；不要改名删除数百个历史实验补丁。
- 用当前 7-worker 快慢差异和现存 per-owner 记录测试是否可以单纯改分片计划缩短整体墙钟；不动编译器实现优先。
- 把需要长远重构的 `MinicType` 耦合、block-local SSA、Machine IR、TargetConfig/ABI/Debug/Runtime 分离留在本节，**不得将它们当成自举前的待完成阻塞项**。
