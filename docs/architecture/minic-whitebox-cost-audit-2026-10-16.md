# MiniC 当前实现：白盒调用量、重复扫描与优先级（第二轮）

**时间**：2026-10-16  
**被审计的代码身份**：`main@02b599f39070ed44ff9f1589d782b7e4bbfbf51f`；本报告只读当前生产 C、现有脚本与历史已保存性能结果。  
**边界**：本轮没有改生产源码、运行新的性能测试或者自举。复杂度是从循环和调用点推导的理论成本，不是 profile 得到的 CPU 占比；没有统计实际进入分支的调用数前不能声称哪个一定最慢。

## 1. 先从真实调用图定量

```text
src/compiler/compiler.c
  parse             minic_parse_c0_program
  verify parsed     AST verifier + FunctionBody ownership
  normalize         minic_c0_program_normalize_casts
  verify normalized AST verifier + FunctionBody ownership
  reachability      minic_c0_program_recompute_inline_emission_references
  specialize        integer  -> symbolic -> boolean range
  finalize          minic_finalize_inline_integer_specialization_references
  core set          per eligible function: minic_core_lower_function
                                          └ core_function_verify #1 (on successful Lower)
  core pruning      minic_prune_inline_specializations_from_core
                    └ reachable Core CALL / FUNCTION_ADDRESS edge → name lookup
  core verify       minic_validate_core_functions → core_function_verify #2 (on currently emitted)
  layout            minic_riscv64_layout_program
  codegen           minic_riscv64_write_c0_program_with_core_functions
                    └ core_function_can_emit → core_function_verify #3 (on emitted)
                       + RV64 ABI/instruction support checks
```

令 `E` = 当前 Program 全部 expression 数，`S` = 全部 statement 数，`F` = 全部函数记录数，`F_L` = 实际需要 Core lowering 的函数数（**不是** `F`），`K_i/K_s/K_b` = 进入相应 transitive specialization 的克隆调用者数，`C` = CALL 表达式数量，`Q` = 可达 Core 调用或取函数地址的边数。每个 Core 函数另有 `B_f/I_f/V_f` 表示基本块/指令/值数。

| 源码与调用条件 | 明确的循环/分配结构 | 理论工作量 | 不可误读之处 |
|---|---|---|---|
| `compiler.c:331+` Integer direct | 一次扫 `E`；按需匹配已有 specialization 变体 | `E` + 变体匹配 | 有克隆上限，不等于无限生成 |
| `compiler.c:423-572` Integer transitive | 已构建 `calls[]`，每 clone 遍历 `C`；候选仍线性搜索变体 | `K_i·C + M_i·V` 量级 | **CALL 索引已在生产源码**，勿重做；整数 source-owner 过滤仍在独立 opt-in patch |
| `compiler.c:1192-1361` Symbolic transitive | 已构建 source owner 映射，但每 clone 仍扫 `E`，在循环内排除已知异主表达式 | `K_s·E + M_s·V` | **Owner 索引已在生产**，不是尚未实现；未知/共享 owner 必须保留回退 |
| `compiler.c:1847-1974` Boolean-range transitive | 对有 range fact 的 clone 扫 `E`，并线性匹配 variant | `K_b·E + M_b·V` | 没有看到等价 source-owner / CALL 索引；但重新使用索引需检查三轮特化改变 AST 的事实有效期 |
| `core_lower.c:16183-16257` 每函数工作区 | 每个函数按整个 TU 的 `S` 分配、写入 `MINIC_CORE_BLOCK_INVALID`，随后随机按全局 StatementId 访问 | `F_L·S` 初始化写入 | 克隆函数可共享源 FunctionBody；只因语句 ID 为全局不能直接改为局部索引 |
| `core_lower.c:12531-12630` if reentry guard | 每次创建 block/statement membership，按 `S` 和 `E` 查 goto/asm-goto/label address | 单次约 `S+E` + 内存分配 | 它是**防止错误删掉可跳入分支**的安全证明，不是空闲循环；多个常量 if 能反复调用 |
| `core_lower.c:14203-14256` switch label reentry | 每个被查询 label 扫 `S`+ `E` | `Q_{label}·(S+E)` | switch 与 goto/asm-goto/计算 goto 的外部入口必须保守 |
| `core_lower.c:15379-15440` normalized loop-label | 每个被查询内部 loop label 扫 `S`+ `E` | `Q_{loop}·(S+E)` | 不能因为名字叫 internal 就忽略外部引用 |
| `core_lower_bool.c:74-157,159-200` zero-only bool | 若确实匹配到局部 `_Bool`，才执行 whole-TU `S+E+Asm` 不变性证明 | `Q_{zero}·(S+E+A)` | 和下述 program_zero_checked 不是同一个证明，不能混为已经全部缓存 |
| `core_lower.c:581-735` 老的 program-zero 证明 | `core_program_bool_is_provably_zero` 全 TU 扫描，但 `program_zero_checked` 在当前函数 local facts 中避免反复做相同证明 | 大体每有资格 local 一次，而非每次读取 | **上一轮若将此与新 zero-only CFG 等同于“没有缓存”是错误的** |
| `compiler.c:2062-2111,2133-2240` Core reachability | 对可达 Core 的每条 CALL / FUNCTION_ADDRESS 用字符串到函数名解析；`minic_core_named_function_id` 从 0 到 `F` 线性找第一次匹配 | `Q·F` 最坏 | Parser 已有 function hash，但 **Compiler/Core reachability 不使用 Parser 的索引**；现阶段不能当成已经索引化 |
| `core_ir.c:1701-1865` Core Verifier | 每块清空整函数 `available_values[V_f]`，再验证所有指令与结果；且成功 lowering、core-validate、codegen preflight 可以各触发一次 | 单次 `B_f·V_f + I_f`，正常 emitted 函数最多三次 | 不可粗暴删除后端公共 API 的验证；Verifier 已作为独立使用者的保护边界 |

`M_i/M_s/M_b` 指实际执行 variant 匹配查询数，`V` 指对应的变体表大小；以上乘积只是上界/结构，不等于所有 TU 都达到最坏情况。

## 2. 代码里已经做好的优化与白盒新发现

**不要重复实现：**

- `parser_core.c:698-891` function-name hash；`parser_global.c:33-230` global hash；`parser_typedef.c:10-185` typedef hash；`parser_enum.c:98-300` enum global hash。作用域内 ordinary/local 仍须遵守最近绑定遮蔽语义。
- `compiler.c:423-435` Integer CALL ID 索引：这是已经进入当前 Runtime 生产源码的算法。
- `compiler.c:1192-1242` Symbolic owner index；`function_body.c:638-722` 的 helper 同样已入生产；未知/共享 owner 走保守路径。
- `core_lower.c:716-721` program_zero_checked：旧局部零值稳定性检查已有一定缓存。
- RV64 Core producer/use 索引、栈槽复用等已纳入生产，不应再次作为「新方案」。

**不应忽略的独立新线索：**

1. `compiler.c:2062-2086` 的 **Core 可达性函数名解析仍为线性**，位于 `minic_prune_inline_specializations_from_core` 的 BFS 调用路径；`src/frontend/parser_core.c` 的 function hash 只属于 Parser 生命周期，不能跨模块直接引用。这是结构明确且能借鉴现有 first-declaration-wins 语义的候选，但实际 Core reachable Q 尚未知。
2. `core_ir.c:1715-1717` 每块 `memset(V_f)` 的成本，是**每次验证算法自己的复杂度问题**；比只删去 1～2 次验证更通用，因为保留所有安全边界也能降低成本。可以考虑按 block generation stamp 或只清理上一个 block 的实际 definitions，必须保留仅块内可用的契约、重复定义检验及边界条件；两种路线仅为候选，尚未验证。
3. `core_lower.c:16231-16234` 的 `F_L·S` 工作区更接近纯冗余的初始化成本，但它按全局 ID 被多个 goto/continue/case handler 访问，不能用未经证明的“本函数 statement 数”替代。可考虑稀疏/epoch 映射或受验证的 FunctionBody scope，需明确 clone 和 source ID 共用。
4. `compiler.c:615-619` 的 `LVALUE_READ` 分支具有恒为 false 的 `&& false` 表达式，是**局部死判断/清理候选**（需验证前置读取纯度、无诊断副作用）；与上面大型性能优化分开。
5. `core_lower.c:13084-13094` 根据硬编码函数名 `genl_op_from_split` 打印 `GENL_IF_DECISION`，未见环境变量控制；这是**生产诊断噪声/教学整洁性候选**，不应未经讨论直接删掉真实错误诊断。其余很多 stderr 打印属于明确失败处理或 trace env gate，不能按关键词批量清理。

## 3. 为什么暂不立即动手改或继续盲测

这一轮将白盒证据分为三类：

1. **可证明的重复**：`F_L·S` 全 TU statement 映射初始化；Verifier 每块 `memset(V_f)`；Codegen 前一份已存在的 Core Verifier 结果再次验证。是否有值得保留的实际 wall 收益仍未确认。
2. **可能极高频，需知道分支进入次数**：`K_s·E` symbolic transitive、`K_b·E` boolean range、`Q·F` Core reachable name lookup、`Q_{reentry}·(S+E)` 控制流外部入口证明。应优先已有 `MINIC_BOOTSTRAP_TRACE`、`MINIC_PARSER_PERF_TRACE`、历史冻结 .i 与前次逐阶段记录。现阶段无数据证明运行次数。
3. **真实 RSS 已大，但不适合自举前贸然动**：`sizeof(MinicExpression)=680`、CALL 4–6% 的大 union；另外 Function 内联最大参数定长数组。此问题单列内存设计实验；不要通过改变数组增长倍率重复已被否定的实验。

重要历史实验证据：
- `docs/ci/linux-performance-causal-investigation-v1.md` 中，整数 transitive 超过 99% 的旧版 Integer Specialization 工作时间；source-owner filter **作为 patch** 已有三文件 A/B 约 `1.5435× / 1.1579× / 1.1514×`。当前 `compiler.c` 只有 CALL index，**不能把这组补丁实验自动算成正式源码收益**。
- 先前 Boolean Domain cache 的 3-TU A/B 已有 `1.79～1.94×`；当前需要按源码/补丁路径确认身份，不应重新测量同一个 Boolean Domain 实验。
- `docs/ci/minic-i-to-s-whole-path-complexity-audit-2026-10-15.md` 已记录 Codegen object layout 重复查询与大 AST；本次保留为候选，但不把它们说成新发现。
- `docs/ci/minic-full-linux-2064-performance-evidence-2026-10-15.md` 保留过去 full-2064 paired A/B 结果，旧测试不同优化组合、不同耗时口径不能冒充当前提交的 wall 对照。

## 4. 下一次静态审计的判定表

| 项目 | 还要核实的最少白盒事实 | 若成立可做的**最小**改变 |
|---|---|---|
| Specialization | clone 共享 source body、生成中是否追加 expression、索引是否有效、fallback 包含未知 owner | 复用已有 source-owner 与 CALL 数据结构，先按函数限缩整数/布尔 transitive 的扫描，保持旧匹配和失败回退 |
| Core reentry | label/goto/asm-goto/label-address 的事实随 Core Lowering 是否发生改变，是否能一次 TU 建立只读 facts | 将每次扫描改为一次构建的**极窄**只读引用索引，不能盲并所有证明 |
| Statement workspace | FunctionBody 内是否能完整映射某函数拥有的 StatementId，clones 继承同一 body 的实际集合 | 利用 generation/稀疏表省去每函数全 TU 初始化，全部存取处必须受检 |
| Core reachability | 可达 CALL edge 的数量及 `assembler_name` 别名、first-match wins 匹配规则 | 若名解析占比可观，用一次性按 name/hash + 完全相同比较语义的小表，而非重复完整扫描 `F` |
| Core Verifier | 实际 `B_f/V_f` 分布；除重复验证外，Verifier 内部可用值数组的读写约束 | 保留三次 API 保护并先降低一次调用的 `B_f·V_f` 工作；仅在这仍显著时评估正式 pipeline 可复用结果 |
| Parser | `MINIC_PARSER_PERF_TRACE` 已有查找 calls/steps，函数/global/typedef/enum 均有哈希 | 只选实际仍高频的 local/ordinary 查找研究，不重复已有 hash |
| 可删除代码 | 真实调用点、预编译条件、历史修复来源和诊断用途 | 只有证据覆盖所有生产路径才删；不要通过文件大小推断 |

**建议的近期顺序**：读完 specialization 的 FunctionBody/所有权约束、Core reentry 的可达性约束后，选 **一个既容易做正确性证明、又有理论数量级改进** 的具体方案；才用已经保存的 2～3 个真实慢 `.i` 测同条件时间/RSS和 `.s`，绝不连试大量未经白盒排除的方案。当前先不做新前端、新后端、SSA/MIR/Pass 框架或大规模 C 文件拆迁。


## 5. 第三层白盒追踪：更直接的 O(F²) 热点（重要补充）

继续沿 `minic_prune_inline_specializations_from_core` 和整数特化的清理路径往内追后，又发现上面 `Q·F` 名字查找以外的重复扫描。**此发现提升了后续优先级，但仍未测 CPU 时间。**

### 5.1 `minic_inline_source_has_noncore_root`：同一个根事实逐函数重复证明

- `compiler.c:2299-2328`：对 `source_id` 依次判断 entry/force_emit，扫描 **所有 `program->functions`** 找是否有 `alias_target == source_id`，再遍历 **所有 `global_objects[*].relocations`** 找对该函数的非 Core 引用。
- `compiler.c:2330-2347`：`minic_inline_source_is_core_reachable_only` 对 **已定义且内部**的函数触发这一 whole-program 根判断。
- `compiler.c:2379-2394` 和 `2415-2422`：`minic_prune_inline_specializations_from_core` 中有 **两趟**遍历 `program->function_count`，可能分别对每个适格内部函数重新调用以上根判断。
- 记 `F_{int}` 为适格内部定义数，`R` 为全局对象 relocation 总数：最坏可达 **`O(F_int·(F+R))`**，最多触发两轮。它不依赖是否存在大量特殊化 clone；没有任何 Core CALL 时也可能触发。
- **很直接的候选**：进入本次 Core pruning 后，一次顺序遍历全部函数 alias、entry/force_emit 与全局 relocation，构建 `noncore_root[FunctionId]` 位图；两趟查询均 O(1)。构建近似 `O(F+R)`。原检查涉及的安全根类型完全保留，内存分配失败可明确选择原始扫描回退。**注意** `function_body.c:773-884` 在较早的“词法/语义引用再计算”阶段就已对 alias/relocation 等进行一次遍历，但它没有保留给后面的 Core pruning 的 root bitmap；可复用其**语义规则**，不能错误复用已被 specialization 改动的 `is_referenced` 或假定两阶段事实相同。
- 这是目前**最适合先做单一小范围、可解释性高的生产重构候选**：职责单一、无新增框架，理论上从二重反复扫描变成一次预计算。须先比对 precompute 与旧单项查询完全一致，连同 alias、global relocation、entry 和 forced emit 用例。

### 5.2 `minic_finalize_inline_integer_specialization_references`：按 clone 重新找剩余源引用

- `compiler.c:1985-2025` 的 `minic_inline_integer_source_has_residual_reference` 对一个源函数扫描函数 alias、全部 relocation 和**整个 `expression_count`**，包含 CALL/函数地址两种情况。
- `compiler.c:2047-2056` 对每个 integer-specialization clone（在 source internal/inline/仍 referenced 的情况下）重新调用该函数。
- 如果一个源函数有多个 clone 且其原始引用仍存在，它可能**重复全 TU 扫描**；理论 `O(K_{active}·(F+R+E))`。若第一次查完后 source 被标记 unreferenced，后续该源函数跳过，因此实际放大由仍有残留引用的源函数和 clone 数决定。
- 此分析也提醒我们：当前工程里**不只 transitive 特化**会出现 Clone×E。整理时应画出整个 specialization lifecycle：建立变体 → 改写调用 → finalize 原源函数引用 → Core prune。不能只在单个整数函数里做优化就宣称解决全部扩增。
- 候选是本轮 finalize 局部重用一份准确的 `source_has_residual_reference[FunctionId]` 结果，或从已改写完成的 Program 一次性扫描建立指向各 FunctionId 的引用位图。需要特别保留函数名 alias/全局 relocation 与两个直接表达式 kind 的约束；不能误将不可达源码的表达式当作已消除，原实现是保守的**语法引用检查**。

### 5.3 重新调整具体行动排序

若优先考虑 **改动最小 + 正确性最易证明 + 避免重复造框架**：**先评估 5.1 的 Core 非 Core 根位图**，再看 5.2 的 specialization 源引用重复扫描；之后才安排 1）Integer/Boolean 的 source-owner/CALL 候选、2）Core statement workspace、3）Core reentry 多次全 TU 扫描、4）Verifier 每块清零等。这个是**代码可维护性优先的行动排序**，不是未经采样确认的 CPU 耗时排名。当前仍没有修改生产代码，项目主目标仍是先轻量收敛、再自举。

**反例提醒**：只有确定了 root facts 的创建时机，才能一次构建；不同 pass 会修改 `is_referenced` / 生成 clone / 改写 Call，不能把跨阶段缓存当作自动永远有效。这也是我们暂时拒绝先引入全局 PassManager/事实缓存框架的原因。


## 6. 第一次生产优化落地：Core pruning 非 Core 根位图

**生产源码提交**：`9e53327489695add451941d5b392c6432f499118`。只改 `src/compiler/compiler.c`，将 `minic_prune_inline_specializations_from_core` 的重复 `noncore root` 判断变为 **pass-local** 只读索引：

- `minic_build_noncore_root_index`：一次线性遍历 `functions[]` 收集 entry、`force_emit`、`alias_target`，一次遍历 `global_objects[*].relocations` 收集函数重定位目标，构建长度为函数数的布尔位图。
- 两次 root classification 读取这份位图；仅 `is_referenced` 在本 Pass 中改动，位图所依赖的字段没有在当前 Pass 内改动。
- **分配失败**时 `noncore_roots == NULL`，保留完全原来的 `minic_inline_source_has_noncore_root` 逐项扫描，绝不因内存不足而漏保留函数。
- 不改变 FunctionBody、Core IR、Symbol lookup、完整 ELF/ABI 处理；没有新增框架、入口、配置项或 CI 工作流。

**静态复杂度**：原先最坏 `O(F_int·(F+R))` 级重复根判定，现在一轮收集 `O(F+R)`，每个函数查询 `O(1)`；新增 `O(F)` 位图空间。实际完整 MiniC wall/RSS 收益 **尚无同输入 A/B**，所以不得宣称已经加速整份 Linux。

**已经执行的门禁**：[Toolchain Focused Regression #38884992042](https://github.com/yituanxing/minic-toolchain/actions/runs/38884992042) 在上述生产 SHA 上 **SUCCESS**，单次共享工具链构建、8/8 独立 focused checks，通过 `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`。Linux Runtime GCC Baseline 同 SHA 对应 run #38884992134 是 **SKIPPED**，不是通过或失败；最近已通过的 2064/QEMU 是旧源码 `24b02474`，**不能冒充本次提交的全量验收**。

**下一门槛**：在真实冻结 `.i`（包含少/多 alias、全局重定位、函数克隆与静态 inline 的小例 + 一份大型 Linux TU）验证新旧 `.s` 相同且取得 wall/RSS；有明显收益再晋级到现有 Linux 2064/QEMU 认证。不要为了这一项独立添加一套性能脚本或新的 workflow。


## 7. 逐源残余引用缓存与 focused 真实结果（2026-10-16）

**变更**：
- `f71cccd7e3dac49871aad6d06cfb9c77e8775986`：`src/compiler/compiler.c` 的 `minic_finalize_inline_integer_specialization_references` 不再对同一个 `specialization_source` 的多个 clone 反复执行 `minic_inline_integer_source_has_residual_reference` 全 TU 扫描。只在**本次 finalization** 内使用 `source_residual_cache[source_id]`，三个状态为未知、仍有引用、无引用。缓存按需分配，OOM 时继续原来的保守扫描；所有 clone 的初始 `is_referenced` 仍先依据原 source 复制，随后才判断 source 是否可退休。不会在不同 TU、不同 specializations 阶段复用，避免过期。
- `73f4cd34` / `2f375ee8`：增强**现有** `tests/compiler/c0/run-inline-emission-reachability-rv64.sh`，在同一个 inline 函数上同时制造三个常数实参调用和一个动态实参调用，检查原函数符号必须保留、已死亡函数不发射、QEMU 最终返回码保持 0；不新建 workflow 或重复验证脚本。

**回归过程，包括失败事实**：
- `f71cccd7` 源码提交的 [Focused T1 #38885665621](https://github.com/yituanxing/minic-toolchain/actions/runs/38885665621) **SUCCESS**。
- 新增测试最初 [#38885995149](https://github.com/yituanxing/minic-toolchain/actions/runs/38885995149) **FAILURE**：生成的测试源第 23 行从新加的多行 C 注释开始，MiniC 报 `expected type name`；该运行的生产编译器已成功构建，失败在 fixture 输入的编译阶段，未执行 QEMU。此现象**尚不能单凭这条日志定为生产注释处理缺陷**；已去掉 fixture 中非必需的文字注释，保持测试表达式和编译器源码不变。若今后需要，另以最小独立注释预处理输入复现，不能把未排除的预处理问题当成此次缓存的回归。
- `2f375ee8` 的 [Focused T1 #38886138281](https://github.com/yituanxing/minic-toolchain/actions/runs/38886138281) **SUCCESS**；真实日志：`MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=3 functions=14`，`INLINE_EMISSION_REACHABILITY_RV64=PASS ... residual_inline=retained ... qemu_rc=0`，`TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`。因此新夹具确实触发了多个克隆，不是无效的普通 `static inline` 烟雾测试。

**性能结论仍为未认证**：两个优化（noncore root 一次位图 + 每源 residual reference memo）均有源码复杂度收益和 focused 正确性证据，**还没有旧/新同输入 Wall/RSS、汇编 SHA 配对**，也没有这两项修改后的 2064 Linux 对象 + QEMU 全量 P1。在此之前不可宣称 Linux 构建加速或完全无运行时回归。下一轮应先复用现有 frozen `.i`/相同 A/B runner 做组合结果的代表文件测量；收益小则保留可读性收益或回退而不是继续叠加算法，随后将自举作为主要目标。
