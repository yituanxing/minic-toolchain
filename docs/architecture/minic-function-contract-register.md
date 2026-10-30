# MiniC 函数级契约与审计台账

> **持续维护，不等于全部函数已审完。** 每个关键函数要回答：谁调用、输入借用还是拥有、可能写什么、返回或失败后谁释放、身份及目标 ABI 不变式是什么。
>
首建：2026-10-30；核对基线 `f4045d8f9`，分支 `refactor/minic-target-language-boundary`。配套：[全局阶段与所有权图谱](minic-global-contract-map.md) · [MC 缺陷台账](minic-issue-register.md)。行号仅辅助定位，**函数名和相对路径才是稳定定位符**。

## 0. 怎样读这份台账

- `TRACE`：本轮实际查看了该函数入口、相关调用方、关键写入及清理链；**不等于对该函数所有条件分支做了动态验证**。
- `PARTIAL`：入口及部分调用路径已核实，存在明确未读分支。
- `INVESTIGATE`：某条路径可能违反契约，证据不足以判定 Bug；对应 MC 条目不能升级为 `OPEN`。
- `KNOWN-ISSUE`：已在 [MC 台账](minic-issue-register.md) 登记，有明确证据等级；这里不重复造 Bug 编号。
- `CONFIRMED-DESIGN`：专门追溯生产者和消费者后排除的表象误报；新证据出现之前不因函数长、数组同序或两路径并存重报。

`R`＝只读借用；`W`＝对调用方拥有的对象进行修改；`OWN`＝函数本地分配并负责清理或显式移交；`XFER`＝有条件地转移所有权。**不能把“借用 Program 指针”简化成“Program 不会发生写入”**：Parser/Normalize/Inline 是 W，Core/Target 原则上 R。

### 阅读与理解分开记账

“看过函数”不等于“掌握契约”，更不等于“证明正确”。后续按两个独立进度轴积累覆盖：

| 阅读轴 | 完成门槛 | 理解轴 | 完成门槛 |
| --- | --- | --- | --- |
| `R0` 未读 | 尚未核对实际函数体 | `U0` 未理解 | 没有产出可信的所有权/行为契约 |
| `R1` 函数体已读 | 输入、分支、分配/释放和错误出口已通读 | `U1` 已建模型 | 写清前置条件、读写集、owner、生命周期、失败语义 |
| `R2` 调用链已读 | 实际 caller、被调函数及其他消费者已追踪 | `U2` 契约交叉核对 | 在生产者/消费者处证明不变式；明确目标和特殊路径的例外 |

验证是第三个独立证据轴：`E0` 只有源码结构、`E1` 真实 SOURCE-TRACE、`E2` 最小反例/ASan/GCC-DIFF/ELF、`E3` 目标 QEMU/原生运行。`E2/E3` 依场景适用，不需要为纯 API 不变式制造无意义的运行测试。现有 `TRACE/PARTIAL` 是此前的粗粒度口径，**不能自动换算为 `R2/U2`**；必须在后续函数复核中逐条升级。

每审一个函数的固定动作是：**阅读函数及调用链 → 复原正确的设计意图 → 核查身份、状态和失败资源 → 仅为非显然不变式补源码注释 → 补本台账 → 问题有证据才写 MC**。发现现有注释与真实语义不符，要先更正注释；不要为了“每函数有注释”重复描述代码执行步骤。

本轮开始的双轴试填（不是完整函数覆盖统计）：

| 函数 | 阅读 | 理解 | 证据/处理 |
| --- | --- | --- | --- |
| `minic_c0_program_add_expression` | R2 | U2 | E1；稳定 ID 和可失效元素地址已在源码注释中明确 |
| `minic_parser_resolve_tag` | R2 | U2 | E1；返回的 Parser tags 借用指针会被 bind 扩容失效 |
| `minic_semantic_snapshot_rollback_probe_expression_types` | R2 | U2 | E1；计数回退不会释放 owned CALL payload（MC-009）；更正过度承诺安全的旧注释 |
| `minic_core_function_verify:verify_block` | R2 | U2 | E1；ValueId 全函数唯一、Value 可用性按块重置的不同职责 |
| `minic_backend_asm_source_form_id` | R2 | U2 | E1；仅记录 source 形式来源，不是实例 cache key（MC-026 已排除） |
| `minic_riscv_emit_record_copy_bytes` | R2 | U2 | E1；forward-copy、scratch 与多 chunk 改写调用方寄存器的契约 |

## 1. Compiler 总入口与阶段管控

| ID · 函数（源码） | 调用与输入/产物 | 状态修改、失败与所有权不变式 | 核验 |
| --- | --- | --- | --- |
| `FN-C01` `minic_compile_preprocessed_file_for_target` — `src/compiler/compiler.c` | `minic-cc:main` 调用；R path/Target，OWN SourceBuffer/Program/CoreSet | 顺序 Parse→双 AST/Body 验证→Normalize→Inline→CoreSet→Prune→Core/Layout 验证→Backend；全程统一析构；失败返回 1，目标路径事务由 emitter 负责。 | TRACE |
| `FN-C02` `minic_validate_core_functions` — `src/compiler/compiler.c` | R Program + CoreSet/status | 严格验证两者 `function_count` 和非空槽；`i` 即 FunctionId；非 definition/未引用内部函数按协议跳过，允许 opt-in census，但普通编译 fail-fast。 | TRACE · CONFIRMED-DESIGN |
| `FN-C03` `minic_prepare_core_function_set` — `src/compiler/core_function_set.c` | R Program/Target，W 调用方初始化的 CoreSet 输出 | 为每个源 FunctionId 分配同序 Core 与 status，创建一次 reentry index，各函数 lower 后释放索引，成功替换旧 output；结构分配失败 false 且不发表半成品。局部 Core lower 的失败写入 status，留给 C02 判断。 | TRACE · CONFIRMED-DESIGN |
| `FN-C04` `minic_core_function_set_destroy` — `src/compiler/core_function_set.c` | W+OWN CoreSet | 析构每个 CoreFunction、两个顶层数组，最后 initialize；即使元素是 skipped/failed 也有有效空结构。 | TRACE |
| `FN-C05` `minic_c0_program_recompute_inline_emission_references` — `src/frontend/function_body.c` | W Program 的 referenced 状态，R source graph | 按 FunctionBody reachability 重新计算源引用；不改变 FunctionId。Body owner/index 与 clone 共享关系有特殊策略，不能将共享 body 本身判为 bug。 | PARTIAL · CONFIRMED-DESIGN |
| `FN-C06` `minic_specialize_inline_integer_calls` — `src/compiler/inline_integer.c` | W Program、R Target | 按常量事实 append 函数 clone 并按 CALL 事实修改引用；源函数与 clone 可共享 body/local range；与符号阶段组合的实例事实缺口见 MC-007。 | TRACE · KNOWN-ISSUE |
| `FN-C07` `minic_specialize_inline_symbolic_calls` — `src/compiler/inline_symbolic.c` | W Program、R Target | 创建符号 clone/表达式并处理 transitive CALL；`Expression*` 跨扩容失效（MC-001），shared CALL 的实例专属 callee 被覆盖（MC-006–008）。 | TRACE · KNOWN-ISSUE |
| `FN-C08` `minic_specialize_inline_boolean_range_calls` — `src/compiler/inline_boolean.c` | W Program、R Target；局部布尔域缓存 | 入口调用后释放本 pass 的缓存；深层条件传播/逃逸策略仍未逐行复核。 | PARTIAL |
| `FN-C09` `minic_prune_inline_specializations_from_core` — `src/compiler/core_reachability.c` | W Program.function.is_referenced，R CoreSet；Backend 提供 policy | Core 已建立后按可达性有选择地标记函数发射资格；RV32/RV64 的 `emit_reachable_core_blocks_only` 为 true、其他目标 false。不是新一轮 AST normalize。 | TRACE · CONFIRMED-DESIGN |

**阶段注意：** `minic_c0_program_validate_function_body_ownership` 在 clone 特化之前执行；clone 复用 canonical body 是刻意降低内存与编译成本。错误在把调用**上下文**的结果保存在共享 `ExpressionId` 上，不在这些 ID 的身份协议本身。

## 2. Frontend：Parser、AST、类型与重映射

| ID · 函数（源码） | 输入、返回、真实所有者 | 修改/指针失效/失败契约 | 核验 |
| --- | --- | --- | --- |
| `FN-F01` `minic_parse_c0_program_for_target` — `src/frontend/parser_function.c` | R source/path/Target，W caller Program；OWN Parser 私有 tags/enum/scopes/cache | 逐 TU 解析后释放 Parser 私有动态表；Program 节点交给 compiler 统一析构；返回 false 不意味着每个先前 AST append 已回滚。 | TRACE |
| `FN-F02` `minic_parser_parse_expression` — `src/frontend/parser_expression.c` | W Parser/Program，返回 ExpressionId | 递归表达式可能追加 Expression、Record、类型及 Parser 元数据；不得在调用前后保存指向可扩容数组的裸元素指针。 | TRACE |
| `FN-F03` `minic_c0_program_initialize` / `minic_c0_program_destroy` — `src/frontend/ast.c` | W+OWN Program | 对 AST arena、名字、Record fields、GNU ASM、CALL overflow payload 等逐计数析构；计数被 probe 缩小但 owned payload 未先释放时有 MC-009 风险。 | TRACE · KNOWN-ISSUE |
| `FN-F04` `minic_c0_program_add_expression` — `src/frontend/ast.c` | R input Expression，W Program；返回稳定 ExpressionId | 深拷贝 CALL overflow（由 Program OWN），再追加/可扩容 expressions；失败释放暂态并维持有效数量。**稳定 ID ≠ 稳定 Expression 指针**，见 MC-001。 | TRACE · CONFIRMED-DESIGN |
| `FN-F05` `minic_c0_program_add_function` — `src/frontend/ast.c` | R name、W Program，返回稳定 FunctionId | 拷贝 name 到 Program OWN；append 可 realloc `functions`；失败不增加 count；同一源 Body 被 clone 共享是另一个明确契约。 | TRACE |
| `FN-F06` `minic_c0_program_add_record` — `src/frontend/ast.c` | R name、W Program，返回稳定 RecordId | 复制 name 并可 realloc `records`；同名 Record 在不同 tag scope 合法；跨递归创建记录后不得再用旧 Record*，见 MC-002/003。 | TRACE |
| `FN-F07` `minic_parser_resolve_tag` / `minic_parser_bind_tag` — `src/frontend/parser_core.c` | R/W Parser 自有 tag 表 | resolve 返回 tags 内借用 pointer，bind 可重新分配表；函数本身的借用规则成立，attribute probe 的复制破坏该规则（MC-004）。 | TRACE · KNOWN-ISSUE |
| `FN-F08` `minic_parser_apply_alignment_attribute` — `src/frontend/parser_attribute.c` | W 原 Parser 和 Program，创建 `probe=*parser` | probe 进行完整语义 expression parse，而非纯 lexer peek；扩容 tag/enum 元表释放原 owner 指针，MC-004/005 ASan 确认。 | TRACE · KNOWN-ISSUE |
| `FN-F09` `minic_parser_parse_static_storage_initializer_value` — `src/frontend/parser_static_initializer.c` | W Parser/Program；依对象 Type/Record/Global 初始化 | 递归处理 aggregate/designator，失败可留下由 Program 管理的中间实体；下层 backward overwrite 路径还须复核（MC-023）。 | PARTIAL |
| `FN-F10` `parse_static_record_constant` — `src/frontend/parser_static_initializer.c` | R Program RecordId，W 全局 initializer | 递归前 snapshot `MinicRecord` 值型头；fields 指向独立分配区，故 Program.records 扩容不影响当前快照。**不能误报为 UAF**。 | TRACE · CONFIRMED-DESIGN |
| `FN-F11` `parse_static_union_constant` — 同上 | 同 F10 | 递归前 snapshot record 与 ID；与未快照的 backward overwrite 分支不是同一个实现。 | TRACE · CONFIRMED-DESIGN |
| `FN-F12` `overwrite_static_zero_record_value` / `_constant` — 同上 | R `Program.records[id]`，递归处理已有零初始化 aggregate | 借用的 `MinicRecord*` 在可能新增记录的嵌套表达式后被继续访问；尚无执行反例，**INVESTIGATING**（MC-023），不应称已证 UAF。 | PARTIAL · INVESTIGATE |
| `FN-F13` `minic_parser_inspect_array_initializer_extent` / `probe_static_array_designator_extent` — 同上 | 以 Parser value snapshot 做 array extent 语义探测 | semantic_snapshot 只按计数恢复 Expression；CALL owned overflow 的未释放问题 MC-009。另有 Parser 元表浅拷贝可能性 MC-017，未证 UAF。 | PARTIAL · INVESTIGATE |
| `FN-F14` `minic_c0_program_normalize_casts` — `src/frontend/cast_normalization.c` | W Program；OWN 暂态表达式表与 remap 映射 | 深拷贝 owned CALL payload、核验外部 ExpressionId 映射，成功时替换旧表；失败保留原表；不应因复制/重排而判为双重所有者。 | TRACE · CONFIRMED-DESIGN |
| `FN-F15` `minic_c0_program_remap_external_expression_ids` — `src/frontend/ast_traversal.c` | W Program 的 statement/ASM/cleanup 等外部 ExpressionId | 先验证整份映射，后无失败地统一 apply；维护稳定 ID 契约。 | TRACE · CONFIRMED-DESIGN |
| `FN-F16` `minic_c0_program_validate_function_body_ownership` — `src/frontend/function_body.c` | R Program；OWN 临时 block/statement/local/ASM/cleanup owner 索引 | 按函数根和语义边查 cross-function 违规，释放临时索引；允许不属于任何 FunctionBody 的 parse-time 常量表达式。 | TRACE · CONFIRMED-DESIGN |

## 3. Core：降级、IR、CFG 事实和 GNU ASM

| ID · 函数（源码） | 谁借用/谁拥有，结果 | 副作用、清理和特殊契约 | 核验 |
| --- | --- | --- | --- |
| `FN-R01` `minic_core_lower_function_with_index` — `src/core/core_lower.c` | R FunctionBody/Target/reentry index，OWN local scratch 与暂态 `lowered` | 降级参数入口/控制流/enum metadata→Core verify→成功 `destroy(output); *output=lowered`；失败析构暂态、不改变原 output。 | TRACE · CONFIRMED-DESIGN |
| `FN-R02` `minic_core_lower_function` — 同上 | NULL-index 的 standalone wrapper | 在没有预索引的调用点使用保守 reentry fallback；有真实消费者，不是旧/新重份 lowering。 | TRACE · CONFIRMED-DESIGN |
| `FN-R03` `minic_core_lower_internal_lower_block` — 同上 | R AST Block，W Core CFG/context/local facts | 按 statement 类型调 if/loop/switch/asm 等；return 后合法 goto/ASM goto 可重入；失败由 R01 的整体事务撤销。 | TRACE |
| `FN-R04` `lower_expression` — `src/core/core_lower_expression.c` | R ExpressionId/Program，W Core 值/效果/facts | 子形式分派遵守 `handled=false` **不得写 Core**、`handled=true` 由该 probe 持有结果的契约。大 dispatcher 不自动是层级混乱。 | TRACE · CONFIRMED-DESIGN |
| `FN-R05` `minic_core_function_append_value_instruction` — `src/core/core_ir.c` | W+OWN Core instructions/values/block-owned ID list | 先 reserve 再 append，成功返回稳定 ValueId；扩容可使 Core 裸 Instruction* 失效，已审关键调用方尚无反例。 | TRACE |
| `FN-R06` `minic_core_function_append_effect_instruction` — 同上 | W+OWN Core instruction/block list | 无 SSA 结果；与 R05 的形式不同，不应为了去重合并有值/无值两类 API。失败不发表新计数，容量可变化。 | TRACE · CONFIRMED-DESIGN |
| `FN-R07` `minic_core_function_verify` — `src/core/core_ir_verify.c` | R Core，OWN seen/value 校验 scratch | 校验独立于 AST 的 shape/CFG/type/value/op 契约；成功的 Lower 在交付前经过验证。**每个 opcode 细则未全量审完**。 | PARTIAL |
| `FN-R08` `minic_core_function_destroy` — `src/core/core_ir.c` | W+OWN Core | 释放嵌套 ASM、参数/签名、block/SSA 表、深拷贝的 source 名称；重新 initialize，不依赖 Program 活着。 | TRACE |
| `FN-R09` `minic_core_local_facts_snapshot` / `_restore` / `_meet` — `src/core/local_facts.c` | R/W context facts；OWN caller-owned snapshot | 分支读取 entry 副本、路径恢复和双分支交集；snapshot 要由调用方 `free`；不同路径逃逸状态保守 OR、不可变证明 AND。 | TRACE · CONFIRMED-DESIGN |
| `FN-R10` `minic_core_local_facts_clear_known` — 同上 | W local facts | 撤销普通 known，但可重新建立已证的 specialization/stable-zero 且无 escape 的 facts；函数名不代表清空所有元数据。 | TRACE · CONFIRMED-DESIGN |
| `FN-R11` `minic_core_reentry_index_create` / `_query` — `src/core/reentry_index.c` | R frozen Program，OWN 索引表；query 可 W 统计计数 | typed goto/ASM-goto/label-address incoming 索引；OOM 退回保守 source scan，不根据缺失索引做负证明；非 const index 是统计器，不是修改 Program。 | TRACE · CONFIRMED-DESIGN |
| `FN-R12` `minic_core_lower_inline_asm` — `src/core/core_lower_asm.c` | R AST/source constraints/Target，W Core ASM | target-qualified→CSR→structured→atomic→memory/scalar 认领；required target form 无法匹配就 fail-closed；不同 probe 不是仅名字相似的死实现。 | TRACE · CONFIRMED-DESIGN |
| `FN-R13` `core_asm_publish_operand_plan` — `src/core/core_lower_asm_operands.c` | R target-qualified operand plan，W Core-owned ASM metadata | 验 `binding.operand_index==i`、tie/memory/数量；**下标是明确定义的 operand 身份**。深层每种约束形状尚未全审。 | PARTIAL · CONFIRMED-DESIGN |
| `FN-R14` `core_asm_try_target_register_contract` — `src/core/core_lower_asm_target.c` | R backend qualifier/AST，构 typed binding | target 提供 exact contract，bind 以 `operand_index=i` 显式写身份，再交 R13 验证；两阶段对应是协议。 | TRACE · CONFIRMED-DESIGN |
| `FN-R15` `core_asm_try_immediate_output_csr` — `src/core/core_lower_asm_structured.c` | R source GNU ASM，W Core CSR lowering | Common Core 有 Linux RISC-V `"rK"` 约束专门识别；是否已被目标完整门控未核到结论。**MC-024 待确认边界**，不当成错编。 | PARTIAL · INVESTIGATE |
| `FN-R16` `core_zero_bool_cfg_condition` / `core_program_bool_is_provably_zero` — `src/core/core_lower_bool.c`、`core_lower_const_facts.c` | R Program/facts；为 IF CFG 与 stable-local facts 分别提供保守证明 | 共享大量扫描但 dead-tail/reentry policy 不同，见 MC-019；不能直接删掉其中一支。 | TRACE · CONFIRMED-DESIGN |

**排除旧报告漂移：** 2026-10-29 早期 Core CFG/副作用相关 P0 已有后续修复与五目标 source + native GNU differential 记录（`docs/architecture/review-2026-10-29-core-cfg-effects.md`）。不能引用更早的 root-review 快照就把旧问题重新标成未修复。

## 4. Target catalog 与 Common：公共层到底负责什么

| ID · 函数（源码） | 数据输入 / 所有权 | 结果与失败边界 | 核验 |
| --- | --- | --- | --- |
| `FN-T01` `minic_target_catalog_all/find/default` — `src/target/target_catalog.c` | R 静态单一 `target_registration.def` X-macro catalog | 未知 target 为 NULL；按注册表精确匹配，非运行时随意兜底。 | TRACE · CONFIRMED-DESIGN |
| `FN-T02` `minic_target_info_for_architecture` — `src/target/target_info.c` | R catalog，对齐同一注册表生成的静态 target descriptor | 索引同序由共同 X-macro 定义保证，不是碰巧一致。懒初始化的线程安全性待确认是否属多线程 API 契约（MC-027）。 | TRACE · INVESTIGATE |
| `FN-T03` `minic_target_invocation_begin` / `_plain_char` / `_short_wchar` — 同上 | 从 canonical 读出 Target + IntegerModel，W caller-local invocation | 全部语言选项写副本；不会改变全局 descriptor，支持同进程不同调用的 char/wchar ABI 差异。 | TRACE · CONFIRMED-DESIGN |
| `FN-T04` `minic_data_layout_type` — `src/target/data_layout.c` | R Program/Target Layout，W caller 输出尺寸/对齐 | 目标 C 存储布局，而非寄存器 ABI；检测 overflow/无效 record 失败。 | TRACE |
| `FN-T05` `minic_backend_for_target` — `src/target/backend.c` | R Target/catalog，共同 X-macro backend 表 | 核对 count，按 architecture 精确选择；未知 target NULL；不向其他 ISA emitter fallback。 | TRACE · CONFIRMED-DESIGN |
| `FN-T06` `minic_backend_asm_source_form_id` / `_capture_asm_operand_source` / `_qualify_inline_asm` — 同上 | R GNU template/constraint，生成 source 形式指纹；目标 machine form 另证 | 生产消费者仅保存/打印 `source_form_id`，没有作为 operand 实例 cache key；不要求包含 symbolic name/label（MC-026 已澄清）。capture 的失败路径可能留下 partial plan，仅成功结果可发布。 | TRACE · CONFIRMED-DESIGN |
| `FN-T07` `minic_program_visit_prepared_functions` — `src/target/common/program_visit.h` | R Program/Core/Target；OWN 每个被选 function 的 `MinicCoreFunctionPlan` | build → 传 const facts/object storage 给同步 target callback → destroy；callback 不能长期持有内部地址；失败/census 均清理。 | TRACE · CONFIRMED-DESIGN |
| `FN-T08` `minic_core_function_plan_build` / `_destroy` — `src/target/common/core_function_plan.c` | R Core/Program/Target，OWN semantic facts、CALL index、object storage | 校验 Core 并准备 ABI-neutral 事实；不放机器寄存器或栈偏移；失败销毁部分资源。 | TRACE · CONFIRMED-DESIGN |
| `FN-T09` `minic_program_emit_write` — `src/target/common/program_emit.h` | R Program/Core/Target，OWN 临时目标文件 | `mkstemp`→prefix→global→between→function→trailer→close→finalize→rename；失败 remove 临时 `.s`，原有公共输出不受半成品影响。 | TRACE · CONFIRMED-DESIGN |
| `FN-T10` `minic_multi_global_emission_plan_build` / `_emit_global_payload` — `src/target/common/global_emit.c` | R AST Global/Target；OWN 临时布局/重定位分类 | 公共存储与初始化器遍历，不编码目标 relocation directive；失败 false，由 T09 回滚写入。 | TRACE |
| `FN-T11` `minic_multi_direct_lp64_function_plan_prepare` / `_destroy` — `src/target/common/direct_function_plan.c` | R Core facts/Target ABI policy；OWN CFG/frame/CALL/object planning | 先 semantic preflight 再 profile/CFG/FRAME/ABI；记录 `failed_stage`，失败由 caller destroy。目标 ABIs 有独立 policy；不是复制一套 A64/Loong ABI。 | TRACE · CONFIRMED-DESIGN |
| `FN-T12` `minic_multi_direct_lp64_emit_function` — `src/target/common/direct_function_emit.h` | R Prepared plan/target hooks，借用 FILE* | 负责一致的 ELF wrapper 和 callback 顺序，具体 prologue/blocks 由目标负责；计划始终销毁；T09 最终撤销失败写入。 | TRACE · CONFIRMED-DESIGN |

## 5. Target 各 ISA 的机器职责与特殊失效路径

| ID · 函数（源码） | 拥有 / 借用的资源 | 成功与失败不变式、现实消费者 | 核验 |
| --- | --- | --- | --- |
| `FN-X01` `minic_x86_64_emit_global` — `src/target/x86_64/global_emit.c` | R Common global plan，W caller FILE* | x86 Section/Reloc/标量指令独立；失败 false，T09 统一回滚。 | TRACE |
| `FN-X02` `minic_x86_64_frame_init_with_storage` / `_destroy` — `src/target/x86_64/frame.c` | OWN SysV/MS-X64 物理 frame、CALL 位置、SSA 槽；可 R 已计划对象储存 | 是否借用 storage 显式标志；初始化失败也由 x86 emitter destroy，不与 A64 DirectLP64 共用物理 ABI。 | TRACE · CONFIRMED-DESIGN |
| `FN-X03` `x86_emit_function` / `minic_x86_64_write_c0_program_with_core_functions` — `src/target/x86_64/codegen.c` | R Program/Core/facts，W FILE*；OWN frame | 调整 SysV/MS-X64 CALL、frame/指令，外层 T09 管输出事务；Typed/Legacy GNU ASM 有不同消费者，尚不能删旧路。 | TRACE · CONFIRMED-DESIGN |
| `FN-A01` `aarch64_emit_prologue` / `_emit_function` / `minic_aarch64_write_c0_program_with_core_functions` — `src/target/aarch64/codegen.c` | R AAPCS64 policy、Core facts，W FILE* | target hooks 提供 x8 indirect result、GP/FP varargs、x29 frame；Common LP64 只统一流程；out-of-range/callback 失败 false。 | TRACE · CONFIRMED-DESIGN |
| `FN-A02` `minic_a64_emit_frame_load` / `_store` / `_address` — `src/target/aarch64/frame_emit.c` | R typed `FrameRef(region, offset)`，选 scratch register，W FILE* | 明确区分 incoming/outgoing/owned SP 区，检查偏移与 scratch alias；不是旧 textual frame patcher。 | TRACE · CONFIRMED-DESIGN |
| `FN-A03` `minic_multi_emit_simple_global` — `src/target/common/global_emit.c`（A64 consumer） | R Global plan，W FILE* | Common `%object`/`@object` 方言匹配需真实 A64 GNU AS 检验（MC-025）；当前仅有源码疑点，不判已失败。 | PARTIAL · INVESTIGATE |
| `FN-V01` `minic_riscv_isa_resolve` — `src/target/riscv/isa_resolve.c` | R march/mabi，W caller ISA 参数 | 先 local resolved 验证再整体提交；严格分 `features`（目标可接受）和 `codegen_features`（MiniC 主动产生）与 ABI。RV32 当前 ILP32/I。 | TRACE · CONFIRMED-DESIGN |
| `FN-V02` `minic_riscv_gpr_abi_place` — `src/target/riscv/gpr_abi_cursor.h` | R/W caller cursor、返回位置 | 八 GPR word-placement 共用；RV32/RV64 宽度、frame/variadic ABI 仍独立；失败前不提交 cursor。 | TRACE · CONFIRMED-DESIGN |
| `FN-V03` `rv32_function_abi_plan_build` — `src/target/riscv32/rv32_abi_plan.c` | R Core/CALL index/Program，OWN 参数/CALL 数组，可借/自产 CALL index | 同序构造 ordinal；按 instruction_id/prepared 核验；失败 destroy 空/部分 plan。双数组同序是显式身份映射。 | TRACE · CONFIRMED-DESIGN |
| `FN-V04` `rv32_frame_init_with_storage` — `src/target/riscv32/rv32_frame.c` | R Program/Core/Target/facts，OWN ILP32 frame/call plan/value offsets | caller 用零 frame 入场；失败可有部分资源，`rv32_frame_destroy` 必须执行；超对齐 varargs stack 边界见 MC-014。 | TRACE · KNOWN-ISSUE |
| `FN-V05` `rv32_func` / `minic_riscv32_write_core_program` — `src/target/riscv32/rv32_emit.c` | R Core/facts/Target，W FILE*；OWN frame/reachability | 消费完整 ABI plan，失败销自己的 frame/scratch，T09 删除私有 `.s`。 | TRACE |
| `FN-V06` `minic_riscv64_core_function_admit_profile` — `src/target/riscv64/core_admission.c` | R verified Core/facts/Program，W caller ABI plan | 验证被支持的调用、对象及 varargs 等族并生成 indexed outgoing calls；不产汇编；失败调用方负责析构半计划。 | TRACE |
| `FN-V07` `minic_riscv64_core_frame_initialize_with_storage` — `src/target/riscv64/frame_layout.c` | R Core/facts/storage，XFER ABI plan 的 parameter/outgoing-call allocations 给 frame | Frame 接手资源并在失败/结束统一析构；跨过 align-down 的 varargs 边界见 MC-015。 | TRACE · KNOWN-ISSUE |
| `FN-V08` `emit_core_function_with_symbol` / `minic_riscv64_write_c0_program_with_core_functions` — `src/target/riscv64/core_codegen.c`、`codegen_function.c` | R Core/facts/Program、W FILE*、OWN asm/frame/scratch | admit→asm plan→frame→reachability→ISA→emit；standalone 会自己准备 facts，production 借用现成 plan，两条路径有真实消费者。 | TRACE · CONFIRMED-DESIGN |
| `FN-V09` `rv64_resolve_prepared_call` — `src/target/riscv64/call_emit.c` | R CALL index、prepared call array | 通过 Common ordinal 查找后核 `count`、`sites[ordinal]`、`instruction_id`；两数组不是“恰好同序”，而是稳定协议。 | TRACE · CONFIRMED-DESIGN |
| `FN-V10` `emit_int128_pair_shift_left` / `minic_riscv64_emit_scalar_instruction` 的 wide shift — `src/target/riscv64/scalar_emit.c` | R verified Core values 的左右操作数类型，W target FILE* | 对 `__int128` RHS 显式拒绝；是否与有效 C/GNU RV64 支持冲突仍待真实输入及差分，见 MC-028；当前不修改机器指令语义。 | PARTIAL · INVESTIGATE |
| `FN-L01` `loongarch64_emit_prologue` — `src/target/loongarch64/codegen.c` | R Common frame context，W FILE* | Loong 特定 SP、s0/ra/sret 与 varargs 保存；Common LP64 仅负责回调编排。 | TRACE |
| `FN-L02` `loongarch64_emit_function` / `minic_loongarch64_write_c0_program_with_core_functions` — 同上 | R Core/facts/storage + Loong ABI Policy；W FILE* | 注入 GPR/FPR/record/sret/varargs callback，借用 T11/T12/T09 事务层；失败不会发布部分 `.s`。 | TRACE · CONFIRMED-DESIGN |
| `FN-L03` `minic_loongarch64_emit_asm_output_store` — `src/target/loongarch64/memory_emit.c` | R Core output lvalue、width/地址、3 scratch；W FILE* | 已有未对齐 C lvalue 的安全 write helper；部分特殊 GNU ASM 跳过它（MC-010），不要为修复复制另一套 store 实现。 | TRACE · KNOWN-ISSUE |
| `FN-L04` `minic_loongarch64_qualify_inline_asm` / `_emit_qualified_structured_asm` — `src/target/loongarch64/asm_contract.c` | R Source 约束 vs R Core typed ASM 的**两个不同阶段** | descriptor-first 与 Linux 精确 fallback 有消费者；不可按函数形似删一个；特殊输出存储 MC-010 仍在。 | TRACE · CONFIRMED-DESIGN |
| `FN-L05` `loongarch64` 线性 variadic cursor 相关 emit — `src/target/loongarch64/residual_emit.c` | R 入口寄存器保存与 caller stack 的连续布局前提 | `direct_function_plan.c` 的 `variadic_requires_public_stack_alignment` gate 拒绝过对齐 + 变参函数；此时不承诺 realign 后的线性 cursor 连续性，属于显式能力边界，非静默错编。 | TRACE · CONFIRMED-DESIGN |

公共代码 `src/target/riscv/word_operations.c`、`record_copy_bytes.c` 已被 RV32/RV64 的实际 machine-emission caller 使用，属于成功复用；x86 的 frame 与 A64/Loong 的 DirectLP64 不合并物理 ABI，同样合理。

## 6. 覆盖、未完成的深读与证据门槛

| 目录 | 本轮工作 | 明确尚未完成 |
| --- | --- | --- |
| `src/compiler/`（11 C/H） | 入口、CoreSet、三类 Inline、reachability 的关键函数审查；上轮曾完成 11/11 文件全面阅读 | `inline_boolean.c` 深层算法的全部分支；跨实例事实组合需带 GNU oracle 修复。 |
| `src/frontend/`（70 C/H） | Parser/AST arena、record/initializer、normalize、Body owner 与语义探测关键点 | 所有函数逐行审查未完成；类型组合、long designator、两处额外 semantic probe。 |
| `src/core/`（45 C/H） | CoreLower、IR append/verify/destroy、CFG facts、ASM 编排与 operand 身份 | 每条 opcode verifier、ASM operand 1600+ 行变体、全部 ABI varargs 分支。 |
| `src/target/`（206 C/H） | Target catalog、通用 emitter/plan，五 ISA 的 function/ABI/frame/asm 主要入口 | 目标长尾特殊指令（A64/Loong Linux ASM）、RV32 整数分派、RV64 CALL 全部形状、ELF/QEMU 动态差分。 |
| `tools/minic*` | `minic` 与 `minic-cc` 的实际 CLI 边界 | 其他工具的完整全链路读审（本轮 MiniC 范围外）。 |

上述 `332` 是上轮记录的**已跟踪 C/H 文件数**，不是本轮审毕函数总数。一个文件即使计为已盘点，也不等于其中每个函数的所有分支都是 TRACE。不得凭台账存在声称“100% 全源函数架构正确”。

审计顺序：

1. 文件清点、生产入口、可达 caller 与独立消费者。
2. 函数 Contract：前置条件、借用/OWN/XFER、数据 identity、状态读写集、失败清理、目标特化边界。
3. 对疑点构造最小实际输入，必要时 GCC、ASan、GNU assembler、ELF oracle 与 QEMU。
4. 有证据后才把 `PARTIAL/INVESTIGATE` 升级为 `KNOWN-ISSUE`；修改代码后同步 MC 条目的修复 commit/精确 HEAD 回归。

## 7. 后续新增函数条目的模板

```markdown
| FN-<模块+永久序号> `symbol` — `src/path.c` |
  入口 caller、前置条件、R/W/OWN/XFER 的对象及返回 |
  对哪些身份/arena/事实/文件有副作用，失败后谁析构；是否有 ABI 例外 |
  TRACE/PARTIAL/INVESTIGATE + MC-xxx 或 CONFIRMED-DESIGN |
```

一条疑点可先记录，不要求立刻改；要删除旧路径或提取公共算法时，必须补齐**所有真实 caller 与独立消费者**，以免重构后又留下功能重复或丢掉专有语义。
