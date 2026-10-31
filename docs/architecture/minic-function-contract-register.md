# MiniC 函数级契约与审计台账

> **持续维护，不等于全部函数已审完。** 每个关键函数要回答：谁调用、输入借用还是拥有、可能写什么、返回或失败后谁释放、身份及目标 ABI 不变式是什么。
>
首建：2026-10-30；第四轮复核基线 `400f4a611`，分支 `refactor/minic-target-language-boundary`。配套：[全局阶段与所有权图谱](minic-global-contract-map.md) · [MC 缺陷台账](minic-issue-register.md)。行号仅辅助定位，**函数名和相对路径才是稳定定位符**。

第五轮函数审计起点：`b0870345b`（在第四轮已发表的 HEAD 上继续）；本轮新增契约按 `F5-*` 编号，不把组条目伪称为单独 C 函数数目，也不将 `.s` 文件当作目标机器运行证据。

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

### 2026-10-30 第二轮独立交叉复扫

六个相互独立的函数级复核分片分别覆盖 Compiler/Driver、Parser 语义、Frontend AST、Core、Target Common+x86+A64、RV32/RV64+Loong。每片先清点文件，再选真正的生产函数追溯调用链；这里的“覆盖”**只代表被列出的函数，不代表分片全部 C/H 函数已审毕**。除了特别标记，以下结论均为 `E1`（源码与调用链），不自动获得 ELF/QEMU 级证明。

| 分片 | 本轮逐函数复核范围 | 阅读和理解仍欠缺之处 |
| --- | --- | --- |
| Compiler/Driver | 20 个关键入口、Inline、CoreSet、reachability、CLI 函数按 R2/U2 重核 | 具体每种 Core/Target 指令语义另审；旧的 legacy/cache OOM 路径仍有调用者 |
| Parser | 12 组函数 R2/U2；静态覆写和数组设计符探测 2 组 R2/U1 | MC-017/023 尚无可复现的非法地址读取 |
| AST/FunctionBody | 15 个函数/组 R2/U2；三个类型/签名分配 API R1/U1 | malformed Program 的 verifier 前置条件尚需独立 API 测试 |
| Core | 控制流、SSA、事实、ASM、reentry 共 24 函数/组；大部分 R2/U2 | verify 的各 opcode 与 value-use 枚举仍未逐条覆盖；`rK` 跨目标门控仍待验 |
| Target Common+x86+A64 | 97 个 C/H 文件清点；26+ 个主要函数/组做过 R2/U2 调用链核验 | A64 真实 GAS object 语法、x86 Linux ASM 长尾未逐项深读 |
| RISC-V+Loong | 93 个文件清点；22 个函数/组 R2/U2，3 个 R1/U1 | RV64 其他 scalar opcode、Loong 大型 GNU ASM 特化、交叉执行证据仍待补 |

新增源码注释只固定**非显然事实**：计数回退是否重用 ID、只读遍历是否物理写入、输入是否允许别名可扩容数组、大偏移寄存器 clobber。发现显式能力缺口先登记并跑独立编译差分；稳定 ID、失败保守回退、ABI 独立 policy 等已证合理设计不拆除。

### 2026-10-30 第三轮复读与独立复现

- Parser 分片对 5 个文件的 40 余个函数追踪实际 consumer；Core 对六个原先遗漏的重模块共 6,132 行逐文件复读；Target source-policy 新增 17 个函数交叉核验，RISC-V/Loong 的 CALL、ASM 新增 24 个函数核验。**这些是分片内的审查活动，不是 332 个源码文件或全部函数 100% 完成。**
- 独立执行补证据：MC-033 `++/--` 和 MC-036 Record conditional 为 x86 GNU 本机错编；MC-034 为 ASan pointer-pair C UB；MC-035/039 为两处不同 arena 的 ASan UAF；MC-028/037/038 为有效 GNU C 的 MiniC `-S` 错拒，目标 GNU 汇编/QEMU 仍需区分。
- 排除了三个易错结论：Loong `aligned(4096)` 先被 Common LP64 frame 的 256 上限安全拒绝，不会发出非法 `addi.d 4095`；Loong PTE 的 `+m` 经 Backend/Core 自动建立有效 memory effect；RV32 legacy ASM 四类 opcode 仍有活跃 consumer，与 structured ASM 分工。**这些发现不分配新的 Bug 编号**。
- 源码注释只固定已证契约和用 `FIXME(MC-xxx)` 标识已定位缺陷；代码算法尚未作为本轮审计的一部分修复。

### 2026-10-30 第四轮：双轴复扫与跨层差分

- 六个并行分片再次沿真实 caller→函数体→callee→消费者阅读，覆盖 Frontend 声明/表达式、Compiler/Inline/Core 可达性、Core CFG 事实、DataLayout/Target Common/x86/A64、RV32/RV64、LoongArch64 特殊 ASM。**完成的仅是下文增量表中逐项列出的 R2/U2，不是 332 文件所有函数已经逐行审完。**
- 新证据：MC-041 `long double` 原生错编、MC-042 packed zero-width field 原生/ELF/跨 TU 错码、MC-043 clone budget 编译错拒、MC-044 参数 tag scope 泄漏、MC-045 Loong `+r` 初始值被清零（ASM-ONLY）、MC-048 RV32 pointer→u64 错拒；MC-014/015 的合法源 `.s` 错误地址关系已补实物。MC-037 的 FPR/futex output qualifier、MC-010 的 packed add_unless 也补了真实 `.s` 证据。
- 排误报：Core switch 后 caller 显式清 facts、短路逻辑有 RHS snapshot/meet；DataLayout 非 packed 零宽字段结果正确；RV64 无剩余 GPR 的对齐变参起点正确；x86 typed/legacy ASM、Compiler legacy scan 仍有消费者；Loong uaccess/AMO 已走安全 store，不属于 MC-010 的 13 个槽。详细证据及暂存输入见 MC 台账。
- 以下新增表格的 `E1` 只代表本轮函数层所有权/调用链模型；即使同一模块有一个 `GNU-NATIVE-DIFF`，也不应让其余函数自动获得运行验证等级。

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
| `FN-C10` `minic_inline_local_boolean_domain` / `_legacy` — `src/compiler/inline_boolean.c` | R Program，W per-pass facts cache | Cache 由 Program 地址与 arena counts 判失效；重新建缓存失败改用活跃的 legacy 全量保守扫描，**不是未删旧代码**。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-C11` `minic_inline_source_has_noncore_root` — `src/compiler/core_reachability.c` | R source graph/cache | 新 source root 索引资源不足时有实际消费者回退到原扫描，不能按两套代码并存删除。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-C12` `minic_read_file` / `minic_compile_preprocessed_file` — `src/compiler/compiler.c` | OWN SourceBuffer；RV64 legacy 入口包装 canonical target | `minic_compile_preprocessed_file_for_target` 仍是真实事务 owner；RV64 兼容 wrapper 是活跃公开入口而不是独立 Compiler 管线。 | R2/U2/E1 · CONFIRMED-DESIGN |

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
| `FN-F17` `minic_parser_begin_scope` / `_end_scope` — `src/frontend/parser_core.c` | W Parser 私有 scope/binding 可见性计数 | Local/tag/enum bindings 仅保存值、ID、Span；离开 scope 只缩回可见性，不销毁仍由 Program 拥有的 Record/Enum 语义实体。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F18` `minic_parser_find_nearest_ordinary_binding` / `_find_scoped_global_object` — 同上 | R scope、local/typedef/scoped extern/enum name | 表达式消费者先检查最近普通绑定的遮蔽，再查全局；文件级声明使用另一合法上下文。全局查询 helper 本身不处理遮蔽不是 Bug。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F19` `minic_parser_name_bound` — 同上 | R Parser binding 表；返回布尔 | 当前内部声明与定义仍在，但 `rg` 无生产/测试消费者；清理候选 MC-031，不能连带删有真实调用者的 `_in_current_scope`。 | R2/U2/E1 · DEBT |
| `FN-F20` `minic_parser_materialize_builtin_va_list_source_type` — `src/frontend/parser_type.c` | W canonical Parser/Program，R source-language policy | Parser 入口预建立 `va_list` 语义身份；speculative copy 因 `cache_owner` 不符不得新增 identity，保护 Program 所有权。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F21` `visit_expression_child_ids` — `src/frontend/function_body.c` | 名义 R `MinicExpression*`，W 局部浅拷贝 | 通用写 visitor 会向共享 CALL overflow buffer 进行**同值写回**；正常单线程无已证错码，但物理 const/并发只读不成立，见 MC-030。 | R2/U2/E1 · DEBT |
| `FN-F22` `minic_verify_build_array_owners` — `src/frontend/ast_verifier.c` | R Program 内层 Record/Function 数组，OWN 暂态 owner 位图 | 在详细内层形状校验前构建可选索引；Parser-built 对象满足形状，但手构 malformed Program 的优雅失败尚待验（MC-032）。 | R2/U1/E1 · INVESTIGATE |
| `FN-F23` `minic_c0_global_object_add_relocation` / `add_global_symbol_relocation` — `src/frontend/ast_global.c` | W Global relocations，R caller 的独立 relocation 快照 | 单一排序插入+去重/类型校验；内层 grow 后才读入 member path，因此输入不能借用同一个 growable relocation buffer，现存 caller 未违规。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F24` `minic_c0_program_set_function_signature` — `src/frontend/ast.c` | W Program function signature，OWN 复制后的参数表 | 先完成新签名规范化/分配，成功才释放旧表；失败不改变已发表签名。 | R1/U1/E1 |
| `FN-F25` `minic_parser_parse_alignof_type_value` — `src/frontend/parser_type_query.c` | R/W Parser 的旧 type-only `alignof` 探测入口 | 当前仅有 `parser_internal.h` 声明和定义，`parser_expression.c:parse_alignof` 已处理类型名及表达式两种真实消费者形式；旧 helper 无调用，入 MC-031 清理候选。 | R2/U2/E1 · DEBT |
| `FN-F26` `parse_static_record_constant` → `parse_static_record_designator_path` → `search_record_field_path` — `src/frontend/parser_static_initializer.c`、`parser_member.c` | R Record 快照与字段路径，W global initializer；field helper 反算 RecordId | 将栈上 `record_snapshot` 传给 helper 后执行无关对象指针相减；ASan pointer-pair 已确认，见 MC-034。**快照保护 arena 扩容的目的本身成立。** | R2/U2/ASAN-POINTER-PAIR · KNOWN-ISSUE |
| `FN-F27` `add_runtime_initializer_once_read` — `src/frontend/parser_runtime_initializer.c` | 借 Program Expression 指针，W 临时 Local/Statement/Expression | 在追加 local-lvalue expression（realloc）后继续取 `value->span`；GNU range fixture 前置两条表达式语句真实 ASan UAF（MC-035）。 | R2/U2/ASAN · KNOWN-ISSUE |
| `FN-F28` `parse_fixed_runtime_nested_array_initializer` — 同上 | 借 `Program.array_types[]` 内的 ArrayType 指针，递归处理各子数组 | 子数组 `sizeof(int[N])` 新增 ArrayType descriptor 使旧指针失效，随后继续读取 `element_type`；14 组类型的容量边界 ASan 已确认（MC-039）。 | R2/U2/ASAN · KNOWN-ISSUE |
| `FN-F29` `parse_runtime_record_designator_target` / designated tail — 同上 | R 提升后的 anonymous MEMBER 表达式，W 最后一个顶层字段游标 | 将最内层 `field_index` 误当顶层位置续接索引，合法 `{.b=2,3}` 被拒绝；仅 `.b=2`、`.b=2,.c=3` 可以正确编译（MC-038）。 | R2/U2/LOCAL-COMPILE-DIFF · KNOWN-ISSUE |
| `FN-F30` `parse_attribute_arguments` / `parse_gnu_attribute_lists` / `consume_object_attribute` — `src/frontend/parser_attribute.c` | R GNU attribute token stream，W call-site collection/policy output | 只有一套语法扫描器；预收集的 fixed-capacity 参数与 streaming callback 是不同调用上下文，超限 fail-close，非重复 Parser。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F31` `apply_section` / visibility / `resolve_alias` — 同上 | R 已声明对象/字面字符串；W attribute/alias semantic metadata | source 文本被有意限制为连写无 escape 的原始字符串，alias 仅解析已知 global，不等于通用 ELF linker 别名实现。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F32` `materialize_array_object_type` / `apply_array_decay` / `parse_one_subscript` — `src/frontend/parser_postfix.c` | R AST ID/array info，W 新 AST/type descriptors | source span、ArrayObjectInfo 先按值快照再 append；与 MC-035 的跨 arena pointer 借用不同，这些增长路径已做必要拷贝。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F33` `parse_indirect_arguments` / `parse_one_indirect_call` / `parse_postfix` — 同上 | R 函数类型 snapshot，W CALL scratch/Expression arena | function-type 参数缓存按值拷贝，CALL overflow 由 Program append 深拷贝；无跨扩容裸 FunctionType 指针滥用证据。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F34` `parse_pointer_record_member` / `parse_record_rvalue_member` / `parse_direct_member` — `src/frontend/parser_member.c` | R RecordId/field path，W MEMBER/ADDRESS_OF AST | 匿名字段先经 path 逐层物化，rvalue 单独保守限定；它们使用稳定 ID，不是静态快照反算 ID 的 MC-034 情况。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-F35` `add_array_object_element_lvalue` / `_assignment` / `lower_runtime_scalar_array_plan` — `src/frontend/parser_runtime_initializer.c` | R ArrayObjectInfo/ExpressionId，OWN plan/action buffer，W AST | 普通元素 lvalue/assignment 在增长前保存 span；range plan 的 evaluate-once 另有 MC-035 UAF，不能把危险推到所有数组初始化函数。 | R2/U2/E1 · CONFIRMED-DESIGN |

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
| `FN-R17` `core_inline_asm_rewind_dead_local_assignment` — `src/core/core_lower_asm_structured.c` | W 当前 block/global Instruction+Value visible counts | 仅当 7/8 条赋值物化恰为本 block 和函数全局连续尾部且 Value definitions 匹配时撤销，ID 可重用；对象表不缩。旧“仅留下 tombstone”注释已纠正。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-R18` `core_runtime_if_copy_facts` — `src/core/core_lower_if.c` | R local facts，OWN 私有 `malloc+memcpy` 快照 | 与活跃的 `minic_core_local_facts_snapshot` 在当前前置/失败规则下重复；两者均有 IF 消费者，后续可集中替换，见 MC-029。 | R2/U2/E1 · DEBT |
| `FN-R19` `core_expression_collect_modified_locals` / `core_block_collect_modified_locals` — `src/core/core_lower_local_effects.c` | R AST expression/block，W caller 变更集合 | 未知表达式效果保守标记所有 local 可能修改；block 版复用同一 source-effect 分类，不能把保守回退当为错判。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-R20` `minic_core_lower_loop_statement` — `src/core/core_lower_loop.c` | R AST loop/reentry，W Core CFG/facts，OWN 快照 | 先隔离已知事实再建立循环 CFG；分配失败保守全失效，异常出口沿统一资源清理。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-R21` `minic_core_visit_instruction_value_uses` — `src/core/value_uses.c` | R Core instruction，调用方提供 visitor | 已证有 Core/Target 共享消费者，但 opcode 分支尚未全量复核；不能写成完整 use-list 验收。 | R1/U1/E1 · PARTIAL |
| `FN-R22` `minic_core_lower_scalar_update` / `minic_core_lower_internal_lower_expression_statement` — `src/core/core_lower_assignments.c`、`core_lower_expression.c` | W Core load/add/store 与 `context->local_integer_constants` 的关系 | **MC-033 已证错编**：独立 `++/--` 语句直达 update 不使局部常量事实失效，通用 value lowering 却会失效；x86 GNU native 差分错误，另四 ISA `-S` 同样折叠为错误分支。 | R2/U2/GNU-NATIVE-DIFF · KNOWN-ISSUE |
| `FN-R23` `minic_core_lower_record_conditional_object` — `src/core/core_lower_record.c` | W Core true/false/merge CFG，R/W 共同 local facts | 相较 `CoreExpressionFactFork` 标量条件式，两臂及 merge 缺 snapshot/restore/meet；x86 GNU native 真实 `f(1)=11` vs GCC `1` 错编（MC-036）。 | R2/U2/GNU-NATIVE-DIFF · KNOWN-ISSUE |
| `FN-R24` `core_bit_field_access_window` / `_load_storage` / `_store_storage` — `src/core/core_lower_bit_fields.c` | R bit width/storage/volatile，W Core load/store | 窄窗口与 full-width 保证不跨 packed storage，公共 RMW 与字节窗口正确；不是可以只按源类型宽度替换的普通 scalar load。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-R25` `minic_core_lower_try_bit_field_assignment` / `_read` / `_compound` / `minic_core_lower_bit_field_update` — 同上 | R bit field AST，W Core value/side-effect | 四类路径都含 mask/sign extend，但 RHS 求值顺序、volatile 重读、prefix/postfix 返回不同；可抽纯位运算 helper，不可把四入口机械合并。 | R2/U2/E1 · DESIGN-REVIEW |
| `FN-R26` `minic_core_lower_abi_va_state_pointer` / `minic_core_linear_vararg_advance` / `minic_core_lower_vararg_scalar` — `src/core/core_lower_varargs.c` | R Target varargs policy，W Core cursor/object | `ABI_CURSOR` 与 `LINEAR_SLOT` 有不同物理 ABI 语义；RV32 多字对齐独立，推进前核算 slot 数防溢出，非重复实现。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-R27` `core_prepare_call_argument` / `core_reload_call_arguments` — `src/core/core_lower_calls.c` | OWN 调用参数快照、R Core SSA/AST | direct/indirect/record 三路径在 CFG split 后复用安全 VALUE reload；callee 求值顺序没有违反 C 的未指定次序。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-R28` `minic_core_lower_direct_call` / `_direct_record_call_object` / `core_lower_indirect_call_impl` — 同上 | W Core CALL/object/return，OWN 暂时参数内存 | 实参和 callee 准入不同，失败释放完整；三个 selector 有各自特化证明条件，不可仅按相似循环删除。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-R29` `core_collect_nested_switch_labels` / `lower_nested_case_switch` / `minic_core_lower_switch` — `src/core/core_lower_switch.c` | R AST switch case graph，W Core CFG | nested reentry 需要单独路径；同一 switch 对 nested labels 检查与随后构造之间两次扫描真实存在，属于小额性能债，非错码。 | R2/U2/E1 · DEBT |

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
| `FN-T13` `minic_core_call_index_build` / `_find` — `src/target/common/core_call_index.c` | R Core CALL sites，OWN 按 InstructionId 排序的索引 | prepared 共享 CALL plan 与 standalone consumer 都有精确 ordinal/InstructionId 协议，非偶然同序数组。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T14` `minic_core_object_storage_acquire` / `_release` — `src/target/common/core_object_storage.c` | R 可借用的 prepared storage，必要时 OWN 本地重建 | 明确区分 borrow 与 own，只有 owns 分支释放；不是两个竞争中的 layout 实现。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T15` `minic_multi_direct_call_plan_types` — `src/target/common/direct_call_plan.c` | R Core/ABI policy，OWN caller call plan | 当前 64 参数上限与每个至多 2 slot 的准入约束，使 A64 outgoing 栈小于短立即数上限；不能仅看 `sub sp,#bytes` 断言即时数越界。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T16` `minic_target_machine_options_resolve` — `src/target/target_machine_options.c` | R CLI ISA flags，W invocation-local TargetInfo | `-march/-mabi` 由 RISC-V family 解析；非法组合失败，不污染 canonical TargetInfo；Driver 只做参数转交。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T17` `minic_target_catalog_for_architecture` — `src/target/target_catalog.c` | R X-macro catalog，返回静态描述符 | 遍历精确匹配 architecture，不依赖 enum 数值排序；Driver 与 TargetInfo 有真实消费者。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T18` `minic_riscv_profile_find` — 同上 | R RISC-V ISA profile，返回只读条目 | `features`（源端/AS 可支持）与 `codegen_features`（MiniC 主动生成）独立；RISC-V `I` baseline 与可选扩展不应混淆。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T19` `aarch_local_fixed_register` — `src/target/source_policy/aarch64.c` | R Source GNU register name，返回资格布尔 | 仅允许指定的 source register 名，不负责后续物理永久保留或机器发射；TargetInfo 回调调用。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T20` `aarch_immediate` — 同上 | R source ASM constraint/immediate | AArch64 `I` 受本 ISA 的 imm12/左移规则约束，不等于 RISC-V 的同名字母 `I`；机器层再验证。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T21` `aarch_named_header_builtin` / `aarch_header_opaque_return` / `aarch_header_attribute` — 同上 | R A64 header source language policy | 允许声明 opaque vector type/return 与有限 PCS 属性不代表启用对应 SIMD/SVE 指令/调用约定 codegen；Header→Parser consumer 已追踪。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T22` `x86_readwrite_hardreg` — `src/target/source_policy/x86_64.c` | R source `+r` hardreg constraint | 源级允许 `rsp` 读写形式；机器 ASM emitter 仍有独立的实体许可，不能据 source gate 外推物理权限。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T23` `x86_source_clobber` — 同上 | R GNU ASM clobber name | `rbx/ebx/flags` 为源语法许可范围，不表示能发出任意 clobber 或跳过后端校验。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T24` `x86_source_function_attribute_value` — 同上 | R GNU source function attribute value | 受控 baseline 属性资格并不强迫 `target("bmi2")` 触发 BMI2 主动生成。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T25` `rv32_local_fixed_register` — `src/target/source_policy/riscv32.c` | R RV32 GNU C source register name | 限制当前源策略为 `a0..a7`；与 RV64 有相似语义但描述两个实际 Target policy。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T26` `rv64_local_fixed_register` — `src/target/source_policy/riscv64.c` | R RV64 源 register identity | 统一 RISC-V 名称归一化后约束物理参数 GPR `a0..a7`；与 RV32 不可仅凭文字相似机械删一路。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T27` `loong_machine_builtin_signature` — `src/target/source_policy/loongarch64.c` | R GNU builtin signature/name，返回受控映射 | 含实参类型及立即数位点资格，Machine emitter 另验最终指令；不是开启全部 Loong intrinsic。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T28` `loong_syscall_alias_names_match` — 同上 | R alias/owner 名称与 ELF identity | 只识别 `sys_*` 与 `__se_sys_*` 配对及 ELF 名对齐，不授予广义声明不兼容豁免。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T29` `loong_syscall_alias_word_pair` — 同上 | R Target ABI / Program 参数对 | 精确校验 Linux syscall alias 宽度、符号、word sanitizer 形状。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-T30` `loong_syscall_alias_abi_compatible` — 同上 | R alias 与 owner 全部形式参数 | 仅被特定 Linux syscall alias source policy 用于窄兼容白名单，不是通用 C prototype 规则。 | R2/U2/E1 · CONFIRMED-DESIGN |

## 5. Target 各 ISA 的机器职责与特殊失效路径

| ID · 函数（源码） | 拥有 / 借用的资源 | 成功与失败不变式、现实消费者 | 核验 |
| --- | --- | --- | --- |
| `FN-X01` `minic_x86_64_emit_global` — `src/target/x86_64/global_emit.c` | R Common global plan，W caller FILE* | x86 Section/Reloc/标量指令独立；失败 false，T09 统一回滚。 | TRACE |
| `FN-X02` `minic_x86_64_frame_init_with_storage` / `_destroy` — `src/target/x86_64/frame.c` | OWN SysV/MS-X64 物理 frame、CALL 位置、SSA 槽；可 R 已计划对象储存 | 是否借用 storage 显式标志；初始化失败也由 x86 emitter destroy，不与 A64 DirectLP64 共用物理 ABI。 | TRACE · CONFIRMED-DESIGN |
| `FN-X03` `x86_emit_function` / `minic_x86_64_write_c0_program_with_core_functions` — `src/target/x86_64/codegen.c` | R Program/Core/facts，W FILE*；OWN frame | 调整 SysV/MS-X64 CALL、frame/指令，外层 T09 管输出事务；Typed/Legacy GNU ASM 有不同消费者，尚不能删旧路。 | TRACE · CONFIRMED-DESIGN |
| `FN-X04` `minic_x86_64_qualify_inline_asm` / typed machine emitter — `src/target/x86_64/asm_contract.c`、`machine_asm.c` | R GNU source contract、Core typed machine ID，W FILE* | Typed ID 精确重验，未知 ID fail-closed；无 ID 的旧 GNU Linux 特例仍有消费者，不能未经路径迁移删除。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-A01` `aarch64_emit_prologue` / `_emit_function` / `minic_aarch64_write_c0_program_with_core_functions` — `src/target/aarch64/codegen.c` | R AAPCS64 policy、Core facts，W FILE* | target hooks 提供 x8 indirect result、GP/FP varargs、x29 frame；Common LP64 只统一流程；out-of-range/callback 失败 false。 | TRACE · CONFIRMED-DESIGN |
| `FN-A02` `minic_a64_emit_frame_load` / `_store` / `_address` — `src/target/aarch64/frame_emit.c` | R typed `FrameRef(region, offset)`，选 scratch register，W FILE* | 明确区分 incoming/outgoing/owned SP 区，检查偏移与 scratch alias；不是旧 textual frame patcher。 | TRACE · CONFIRMED-DESIGN |
| `FN-A03` `minic_multi_emit_simple_global` — `src/target/common/global_emit.c`（A64 consumer） | R Global plan，W FILE* | Common `%object`/`@object` 方言匹配需真实 A64 GNU AS 检验（MC-025）；当前仅有源码疑点，不判已失败。 | PARTIAL · INVESTIGATE |
| `FN-A04` `minic_multi_direct_visit_global` — `src/target/common/direct_program_emit.c` | R Target global config 与 Program global，W FILE* | 非 extern 定义走公共 simple-global，未传 dialect；A64 config `%object` 当前只影响 alias，真实路径差异已证，GAS 结果未知（MC-025）。 | R2/U1/E1 · INVESTIGATE |
| `FN-V01` `minic_riscv_isa_resolve` — `src/target/riscv/isa_resolve.c` | R march/mabi，W caller ISA 参数 | 先 local resolved 验证再整体提交；严格分 `features`（目标可接受）和 `codegen_features`（MiniC 主动产生）与 ABI。RV32 当前 ILP32/I。 | TRACE · CONFIRMED-DESIGN |
| `FN-V02` `minic_riscv_gpr_abi_place` — `src/target/riscv/gpr_abi_cursor.h` | R/W caller cursor、返回位置 | 八 GPR word-placement 共用；RV32/RV64 宽度、frame/variadic ABI 仍独立；失败前不提交 cursor。 | TRACE · CONFIRMED-DESIGN |
| `FN-V03` `rv32_function_abi_plan_build` — `src/target/riscv32/rv32_abi_plan.c` | R Core/CALL index/Program，OWN 参数/CALL 数组，可借/自产 CALL index | 同序构造 ordinal；按 instruction_id/prepared 核验；失败 destroy 空/部分 plan。双数组同序是显式身份映射。 | TRACE · CONFIRMED-DESIGN |
| `FN-V04` `rv32_frame_init_with_storage` — `src/target/riscv32/rv32_frame.c` | R Program/Core/Target/facts，OWN ILP32 frame/call plan/value offsets | caller 用零 frame 入场；失败可有部分资源，`rv32_frame_destroy` 必须执行；超对齐 varargs stack 边界见 MC-014。 | TRACE · KNOWN-ISSUE |
| `FN-V05` `rv32_func` / `minic_riscv32_write_core_program` — `src/target/riscv32/rv32_emit.c` | R Core/facts/Target，W FILE*；OWN frame/reachability | 消费完整 ABI plan，失败销自己的 frame/scratch，T09 删除私有 `.s`。 | TRACE |
| `FN-V06` `minic_riscv64_core_function_admit_profile` — `src/target/riscv64/core_admission.c` | R verified Core/facts/Program，W caller ABI plan | 验证被支持的调用、对象及 varargs 等族并生成 indexed outgoing calls；不产汇编；失败调用方负责析构半计划。 | TRACE |
| `FN-V07` `minic_riscv64_core_frame_initialize_with_storage` — `src/target/riscv64/frame_layout.c` | R Core/facts/storage，XFER ABI plan 的 parameter/outgoing-call allocations 给 frame | Frame 接手资源并在失败/结束统一析构；跨过 align-down 的 varargs 边界见 MC-015。 | TRACE · KNOWN-ISSUE |
| `FN-V08` `emit_core_function_with_symbol` / `minic_riscv64_write_c0_program_with_core_functions` — `src/target/riscv64/core_codegen.c`、`codegen_function.c` | R Core/facts/Program、W FILE*、OWN asm/frame/scratch | admit→asm plan→frame→reachability→ISA→emit；standalone 会自己准备 facts，production 借用现成 plan，两条路径有真实消费者。 | TRACE · CONFIRMED-DESIGN |
| `FN-V09` `rv64_resolve_prepared_call` — `src/target/riscv64/call_emit.c` | R CALL index、prepared call array | 通过 Common ordinal 查找后核 `count`、`sites[ordinal]`、`instruction_id`；两数组不是“恰好同序”，而是稳定协议。 | TRACE · CONFIRMED-DESIGN |
| `FN-V10` `emit_int128_pair_shift_left` / `minic_riscv64_emit_scalar_instruction` 的 wide shift — `src/target/riscv64/scalar_emit.c` | R verified Core 左右整数值及类型，W target FILE* | 左右移均显式拒绝 `__int128` RHS；合法 C 用宽计数已本机复现 RV64 `-S` 拒绝，窄计数 PASS（MC-028）。目标 GNU RV64 AS/执行仍待验证。 | R2/U2/LOCAL-COMPILE-DIFF · KNOWN-ISSUE |
| `FN-V11` `minic_riscv64_emit_incoming_parameter_object` / `emit_core_call_argument` / `emit_core_call_arguments` — `src/target/riscv64/call_emit.c` | R admission ABI plan/Core params，W target FILE* | 根据实际 ABI 位点处理 record reference、宽整数/浮点拆分与堆栈实参；direct/indirect 都共享同一传送算法。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-V12` `rv64_emit_call_record_result` / `minic_riscv64_emit_direct_call` / `_indirect_call` — 同上 | R prepared CALL index/返回分类，W target FILE* | 直接/间接 CALL 均消费同一 `rv64_resolve_prepared_call` 身份协议、参数与返回类别，不应复制一套 ABI 算法。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-V13` `rv32_emit_scalar_call_arguments` / `rv32_emit_call` / `rv32_emit_parameter_object` / `rv32_emit_record_return` — `src/target/riscv32/rv32_call.c` | R ILP32 call plan、W GPR/stack/record buffers | 先布置栈实参再装 `a0..a7`，避免下一条 materialize 覆盖前值；hidden sret 最后设 a0，符合已规划的 RV32 ABI 位置。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-L01` `loongarch64_emit_prologue` — `src/target/loongarch64/codegen.c` | R Common frame context，W FILE* | Loong 特定 SP、s0/ra/sret 与 varargs 保存；Common LP64 仅负责回调编排。 | TRACE |
| `FN-L02` `loongarch64_emit_function` / `minic_loongarch64_write_c0_program_with_core_functions` — 同上 | R Core/facts/storage + Loong ABI Policy；W FILE* | 注入 GPR/FPR/record/sret/varargs callback，借用 T11/T12/T09 事务层；失败不会发布部分 `.s`。 | TRACE · CONFIRMED-DESIGN |
| `FN-L03` `minic_loongarch64_emit_asm_output_store` — `src/target/loongarch64/memory_emit.c` | R Core output lvalue、width/地址、3 scratch；W FILE* | 已有未对齐 C lvalue 的安全 write helper；部分特殊 GNU ASM 跳过它（MC-010），不要为修复复制另一套 store 实现。 | TRACE · KNOWN-ISSUE |
| `FN-L04` `minic_loongarch64_qualify_inline_asm` / `_emit_qualified_structured_asm` — `src/target/loongarch64/asm_contract.c` | R Source 约束 vs R Core typed ASM 的**两个不同阶段** | descriptor-first 与 Linux 精确 fallback 有消费者；不可按函数形似删一个；特殊输出存储 MC-010 仍在。 | TRACE · CONFIRMED-DESIGN |
| `FN-L05` `loongarch64` 线性 variadic cursor 相关 emit — `src/target/loongarch64/residual_emit.c` | R 入口寄存器保存与 caller stack 的连续布局前提 | `direct_function_plan.c` 的 `variadic_requires_public_stack_alignment` gate 拒绝过对齐 + 变参函数；此时不承诺 realign 后的线性 cursor 连续性，属于显式能力边界，非静默错编。 | TRACE · CONFIRMED-DESIGN |
| `FN-L06` `minic_loongarch64_emit_sp_mem` / `_frame_mem` — `src/target/loongarch64/frame_emit.c` | W FILE*，R payload reg/offset，使用 `$t3` 大位移寻址 | 大偏移存储要求 payload 不能同时在 `$t3`，否则地址合成覆盖原值；当前生产 caller 未发现此类形状，作为调用约束注释，不判已证 Bug。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-L07` `loong_linux_cpucfg_source` / `_emit`、`loong_linux_frametrace_csr_source` / `_emit` — `src/target/loongarch64/asm_contract.c` | Source R GNU ASM output/cv type，Emitter R Core pointer pointee，W FILE* | Source 使用 `minic_type_unqualified` 接受 volatile，Emitter 直接 `minic_type_equal` 比较未去修饰的 pointee；cpucfg/CSR 均有 GNU C 合法 + MiniC `-S` 非 volatile PASS/volatile FAIL 对照（MC-037）。 | R2/U2/LOCAL-COMPILE-DIFF · KNOWN-ISSUE |
| `FN-L08` `loong_linux_cacop_source` / `_emit` — 同上 | R GNU CACOP 模板/寄存器/内存效果，W FILE* | 源资格和机器编码各执一段，不可改为裸文本替换；与 source policy/target-qualified ASM 注册表共享 14-form 匹配。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-L09` `loong_pte_source` / `_emit` — 同上 | R GNU `+m` output/effect，W FILE* | `backend.c` 将读写内存 operand 自动映成 `implicit_memory_effect`，Core 传播 effective effect，Emitter 校验真实满足；先前 clobber 不一致疑点已排除。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-L10` `loong_percpu_indexed_source` / `_emit` — 同上 | R per-CPU register role、W target machine load/store | 精确 base `$r21` 与原 GNU indexed operand；与 MC-037 类似的 cv source/emitter 比对尚未独立做动态复现。 | R2/U2/E1 · INVESTIGATE |
| `FN-L11` `loong_linux_fpr_transfer_source` / `_emit` — 同上 | R 32 FPR 的物理映射，W FILE* | 传输保留 bit pattern，不是数值 float conversion；cv source/emitter 的同形嫌疑仍需单独 fixture。 | R2/U2/E1 · INVESTIGATE |
| `FN-L12` `loong_linux_frametrace_source` / `_emit` — 同上 | R pt_regs 物理布局，W FILE* | 固定寄存器保存顺序不能抽成一般 memcpy；cv 规则与 cpucfg 同形但本形式仍缺独立差分。 | R2/U2/E1 · INVESTIGATE |
| `FN-L13` `loong_linux_descriptor_qualify` / `_emit` — 同上 | R 同一 14-form target ASM descriptor registry，W Core typed form/FILE* | Source gate 与机器 emitter 用的是同一注册表的**不同阶段**，不是新旧竞争的两份 ASM 解释器。 | R2/U2/E1 · CONFIRMED-DESIGN |
| `FN-L14` `loong_linux_vdso_source` / `_emit` — 同上 | R Linux vDSO syscall 源模板及 clobber，W FILE* | 源语法和机器发射双精确门控用于保留固定 syscall ABI，无证据表明第二关可删除。 | R2/U2/E1 · CONFIRMED-DESIGN |

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

## 8. 第四轮增量函数契约（基线 `400f4a611`）

下面仅登记本轮新交叉阅读或重新确认的**独立函数级入口**。`R2/U2/E1` 表示已看函数体、调用方与关键消费者/owner，并有源码证据；不代表所有输入动态证明正确。一个函数同时涉及已证缺陷时另列 MC 编号及其实际差分等级。多架构的 source gate 和 machine emit 即便共用名字，也按不同生命周期分别记。

### 8.1 Frontend：Declaration / Type / Record / Expression（25 项）

| ID · 符号与源码 | 输入/拥有者 → 真实消费者与契约 | 本轮结果 |
| --- | --- | --- |
| `F4-F36` `minic_parser_parse_pointer_qualifier_sequence` — `parser_declarator.c` | W Parser token，返回带深度限制的各层指针限定符；caller 在组合 Declarator 前使用位掩码，不分配独立 owner。 | R2/U2/E1 |
| `F4-F37` `minic_parser_parse_function_parameter_suffix` — 同上 | W Parser/Program，调用 parameter-list Parser；固定签名数组交 `declaration_sema`，参数 tag 作用域合同须修（MC-044）。 | R2/U2/E1 · MC-044 |
| `F4-F38` `minic_parser_parse_parenthesized_pointer_to_array_declarator` — 同上 | R/W 数组层次及 pointer qualifiers，向 type materialization 传完整括号优先级；不能与普通数组后缀合并后丢维度。 | R2/U2/E1 |
| `F4-F39` `parse_array_suffix_syntax` — 同上 | 只收集 token/suffix，最多 8 层，尚不把每层强行实例化为 ArrayType；由下一函数消费。 | R2/U2/E1 |
| `F4-F40` `minic_parser_parse_array_declarator_suffix` — 同上 | W Program type arena，经 `minic_declaration_materialize_array_suffix` 建实际数组 type；后缀 syntax 与 semantic 分步。 | R2/U2/E1 |
| `F4-F41` `minic_declaration_materialize_array_suffix` — `declaration_sema.c` | R 保存的类型描述，W caller Program.array_types；多维构造失败按 descriptor count 回退，不代表复合 owned CALL payload 也可纯 count rollback。 | R2/U2/E1 |
| `F4-F42` `minic_declaration_instantiate_incomplete_array_object_type` — 同上 | R element Type（递归前拷值），W Program 新数组 descriptor；对象实例化不能在 typedef 源类型上就地修改。 | R2/U2/E1 |
| `F4-F43` `minic_declaration_merge_external_array_composite_type` — 同上 | R/W composite array bound，递归仅调整已有 bound、不追加类型 arena；局部 `declared_array` 指针无已证跨 realloc 风险。 | R2/U2/E1 · 排误报 |
| `F4-F44` `minic_declaration_build_nested_function_pointer_type` — 同上 | W Program FunctionType/ArrayType 与局部两阶段签名拼接；失败回滚临时 descriptor 而不改源声明 owner。 | R2/U2/E1 |
| `F4-F45` `minic_parser_parse_type_specifiers` — `parser_type.c` | W Parser，`typeof` 由稳定 ExpressionId 读取 operand，返回 MinicType 值型描述；不保留可扩容 Expression*。 | R2/U2/E1 |
| `F4-F46` `minic_parser_parse_type_name_preserving_incomplete` — 同上 | W Parser/Program，分流 abstract array/pointer/function pointer 的括号层次并保留 incomplete 身份；参数列表会调用，MC-044 是 caller scope 的错。 | R2/U2/E1 |
| `F4-F47` `parse_record_bit_field_width` — `parser_record.c` | W Parser const-eval，R field type/target layout，返回宽度并拒绝非法大于 storage 的 bitfield；与物理 packed zero-width 放置是两个不同阶段（MC-042）。 | R2/U2/E1 |
| `F4-F48` `parse_record_field_declarator` — 同上 | W Program.records，递归声明中继续使用旧 Record* 可 UAF；这是已有 MC-002，不重复立新 issue。 | R2/U2/E1 · MC-002 |
| `F4-F49` `parse_record_field` — 同上 | W Record fields，追加后的消费者按稳定 RecordId 重新取实体；多 declarator 字段不应被 MC-002 的单一路径泛化。 | R2/U2/E1 · 排误报 |
| `F4-F50` `minic_parser_parse_record_definition_specifier` — 同上 | W Parser tags 和 Program Record，使用当前作用域 bind；其入口本身在 prototype 参数中被错误放置到外层 scope（MC-044）。 | R2/U2/E1 · MC-044 |
| `F4-F51` `parse_floating` — `parser_expression.c` | W Program Expression floating constant；先 `strtod` 成 double 再转 long double 内部 bits，造成已证 x86 原生精度错编（MC-041）。 | R2/U2/GNU-NATIVE-DIFF · MC-041 |
| `F4-F52` `parse_function_reference` — 同上 | R Program FunctionId/签名、W function-pointer Expression；无新 Function arena append，借用不跨相关扩容。 | R2/U2/E1 |
| `F4-F53` `parse_call_argument` — 同上 | W Parser Expression，递归处理实参后重新以 ID 获取 Call/Expression；没有跨可扩容 arena 保存旧元素指针。 | R2/U2/E1 · 排误报 |
| `F4-F54` `minic_parser_expression_parse_call_arguments` — 同上 | W CALL 参数 scratch/AST，`MinicFunction` 先按值快照，避免实参递归 parse 令 Program.functions realloc 失效。 | R2/U2/E1 · 排误报 |
| `F4-F55` `parse_primary` — 同上 | R 最近普通 binding（含遮蔽），W direct CALL 和 overflow 参数；owned payload 最终由 AST append 深拷贝，Parser 不应偷留它。 | R2/U2/E1 |
| `F4-F56` `parse_sizeof` — 同上 | W 类型与表达式 arena；VLA 动态 extent 与固定 `sizeof` 不同，array descriptor append 不直接证明 Expression 指针 UAF。 | R2/U2/E1 |
| `F4-F57` `normalize_float_binary_operands` — 同上 | W CAST Expression，添加左转换后重新获取右 Expression；多次追加导致旧裸指针失效的路径已回避。 | R2/U2/E1 · 排误报 |
| `F4-F58` `parse_function` — `parser_function.c` | W 声明/定义实体，R 先解析的参数列表；函数 body `begin_scope` 发生在参数解析之后，tag identity 泄漏（MC-044）。 | R2/U2/LOCAL-COMPILE-DIFF · MC-044 |
| `F4-F59` `minic_parse_c0_program_for_target` — 同上 | OWN TU Parser 的 tags/scopes/enum tables，W caller Program；parse 终止时独立清理 Parser-owned metadata，不能把源文件 tag 都当 Program arena 自由可见。 | R2/U2/E1 |
| `F4-F60` `minic_parser_parse_parameter_list` — `parser_function_parameters.c` | W parameter types、名字与 Parser tag bind；prototype、function body、nested function pointer 各需不同 scope 结束点，当前缺失（MC-044）。 | R2/U2/LOCAL-COMPILE-DIFF · MC-044 |

### 8.2 Compiler / Inline / Reachability（33 项）

| ID · 符号与源码 | 真实消费者及 owner/身份契约 | 本轮结果 |
| --- | --- | --- |
| `F4-C01` `minic_inline_integer_specialization_matches` — `inline_integer.c` | direct/transitive candidate 扫描只比较整数 known/bits；符号/布尔阶段稍后独立引入，不能按未比较符号 facts 判错。 | R2/U2/E1 |
| `F4-C02` `minic_add_inline_integer_specialization` — 同上 | W Program.functions，OWN 新名字/签名，body/local IDs 故意与 source 共享；整数、符号和 bool 特化 consumer 都复用此创建器。 | R2/U2/E1 |
| `F4-C03` `minic_inline_specialization_argument_value` — 同上 | R Program/Target/caller facts，递归追 CAST 与形参整数事实；不追加 Program arena，返回 typed 常量值。 | R2/U2/E1 |
| `F4-C04` `minic_specialize_inline_integer_calls` — 同上 | W shared CALL FunctionId/append clones，Compiler 调；owner index 分辨单一 call owner，OOM 回扫。超 2048 实例硬拒绝（MC-043）；CALL 实例 facts 另见 MC-006。 | R2/U2/LOCAL-COMPILE-DIFF · MC-043 |
| `F4-C05` `minic_inline_symbolic_static_expression_depth` — `inline_symbolic.c` | R static address expression tree、Target；有界递归、保守拒绝 local lvalue 引用，供闭包/资格调用。 | R2/U2/E1 |
| `F4-C06` `minic_inline_close_symbolic_expression_depth` — 同上 | W Program.expressions，返回稳定 ExpressionId；递归保存旧 Expression* 跨 arena 扩容读旧地址（MC-001）。 | R2/U2/ASAN · MC-001 |
| `F4-C07` `minic_inline_symbolic_argument_expression` — 同上 | R caller clone facts，可能通过闭包 W Program expressions；外层消费者扩容后重取表达式 ID，不应把内层 MC-001 推到此处。 | R2/U2/E1 |
| `F4-C08` `minic_inline_symbolic_integer_inputs_are_closed` — 同上 | direct/transitive symbol clone admission；要求无关整数参数也已知，造成已登记 MC-008 的拒编。 | R2/U2/GCC-DIFF · MC-008 |
| `F4-C09` `minic_inline_symbolic_variant_matches` — 同上 | R candidate 全部 integer/symbolic facts；key 必须含符号 ExpressionId，不能单以 clone count 判身份。 | R2/U2/E1 |
| `F4-C10` `minic_add_inline_symbolic_specialization` — 同上 | 通过整数 clone creator W Program clone，再补符号 facts；被 direct/transitive 和 bool 阶段共享，不独立复制 Body。 | R2/U2/E1 |
| `F4-C11` `minic_specialize_inline_symbolic_calls` — 同上 | Compiler 调用，W Program/clones/CALL refs；owner index/full fallback 有真实路径，shared CALL 污染及混合 facts 是 MC-006/007。 | R2/U2/E1 · MC-001/006/007/008 |
| `F4-C12` `minic_inline_boolean_domain_value` — `inline_boolean.c` | R typed AST 的 _Bool domain，遇不确定 side effect 保守拒；legacy scanner 和 cache builder 共享基础证明。 | R2/U2/E1 |
| `F4-C13` `minic_inline_direct_local_target` — 同上 | R exact LOCAL Expression→LocalId；不猜测经 pointer/alias 的 indirect lvalue；被两个 scanning strategy 复用。 | R2/U2/E1 |
| `F4-C14` `minic_inline_local_boolean_domain_legacy` — 同上 | R 全部 statement/expression/ASM 输出，无额外分配；cache 构造 OOM 时由主入口调用，是真实 fallback 非死实现。 | R2/U2/E1 · 排误报 |
| `F4-C15` `minic_inline_build_local_boolean_domain_cache` — 同上 | OWN calloc scratch 和 values 位图，全部构造完成后才发表 cache；分配失败由 caller 回到 legacy 扫描。 | R2/U2/E1 |
| `F4-C16` `minic_inline_local_boolean_domain` — 同上 | R Program，W pass-local cache；按 arena counts 验失效，build 失败仍给保守答案，不直接令全 TU 失败。 | R2/U2/E1 |
| `F4-C17` `minic_inline_boolean_range_argument` — 同上 | R typed caller argument 和 source fact、W used-caller-fact flag；direct/transitive consumer 准备复合 clone key，不改 AST。 | R2/U2/E1 |
| `F4-C18` `minic_inline_boolean_range_variant_matches` — 同上 | R integer/symbolic/bool 全部 facts，避免同一 source 的两个不等价 bool clone 冲突。 | R2/U2/E1 |
| `F4-C19` `minic_inline_boolean_copy_existing_facts` — 同上 | R clone facts，W caller stack facts vector；添加 bool 事实前保留整数和符号事实。 | R2/U2/E1 |
| `F4-C20` `minic_add_inline_boolean_range_specialization` — 同上 | W Program clone + bool facts，复用 symbolic clone owner；不应独立 append 一套无关签名。 | R2/U2/E1 |
| `F4-C21` `minic_specialize_inline_boolean_range_calls_for_pass` — 同上 | direct W shared CALL，transitive clone-only，Core 后来在 caller 上选中；1024 限额后硬失败（MC-043）。 | R2/U2/LOCAL-COMPILE-DIFF · MC-043 |
| `F4-C22` `minic_specialize_inline_boolean_range_calls` — 同上 | Compiler 调用，OWN pass-local state，失败后也无条件释放；不能将 scratch cache 误判全局泄漏。 | R2/U2/E1 |
| `F4-C23` `minic_inline_integer_source_has_residual_reference` — `inline_reference.c` | R AST/alias/global relocation/entry/force_emit 根；OOM 时兜底保守扫描，包括暂无 FunctionBody owner 的表达式。 | R2/U2/E1 |
| `F4-C24` `minic_finalize_inline_integer_specialization_references` — 同上 | W is_referenced，OWN optional source-wide cache；不能在 Core lowering 前误删尚由非 Core 根引用的 source。 | R2/U2/E1 |
| `F4-C25` `minic_core_named_function_id` — `core_reachability.c` | R ELF symbol→FunctionId，返回稳定 ID；真实调用方 enqueue source/clone edges。 | R2/U2/E1 |
| `F4-C26` `minic_enqueue_core_named_function` — 同上 | R lookup，W queue/mark bitmap；未知外部函数不生成虚假的内部 Core 节点。 | R2/U2/E1 |
| `F4-C27` `minic_walk_reachable_core_edges` — 同上 | R Core CALL/FUNCTION_ADDRESS/CFG，OWN 块 mask/queue；RV 特定 reachable-block policy，间接分支保守扫描全部。 | R2/U2/E1 |
| `F4-C28` `minic_inline_source_has_noncore_root` — 同上 | cache OOM 时真实回退到 alias/global/entry/forced symbol 全扫描；不能作为旧重构残留删除。 | R2/U2/E1 · 排误报 |
| `F4-C29` `minic_build_noncore_root_index` — 同上 | OWN non-Core root bitmap，失败不给假阴性，调用方改用上项 slow scan。 | R2/U2/E1 |
| `F4-C30` `minic_inline_source_is_core_reachable_only` — 同上 | R root index/source flags，给 Core-only 剪枝证据；force_emit 经 FunctionBody 预计算 is_referenced，因此不会被预筛漏掉。 | R2/U2/E1 |
| `F4-C31` `minic_prune_inline_specializations_from_core` — 同上 | W Program is_referenced，OWN queues/bitmaps，R 完整 CoreSet；仅删明确无 Core/外部根的发射资格。 | R2/U2/E1 |
| `F4-C32` `minic_core_lower_direct_call` 的 clone selector — `core_lower_calls.c` | R integer→symbolic→boolean 完整事实，在具体 caller 上按实例选择，未修改 Program；与 shared CALL 事实错误属于相邻但不同层。 | R2/U2/E1 · MC-006/007 |
| `F4-C33` `minic_perf_program_build_expression_source_owners` — `frontend/function_body.c` | R FunctionBody owner graph，W caller owner bitset；ambiguous 标记保守，分配失败走慢扫描，被三个 Inline pass 实际消费。 | R2/U2/E1 |

### 8.3 Target DataLayout / Common Global / x86 / AArch64（25 项）

| ID · 符号与源码 | 输入/消费者/失败行为 | 本轮结果 |
| --- | --- | --- |
| `F4-T01` `minic_data_layout_record_pack` — `target/data_layout.c` | R packed/pragma metadata→pack cap，供 Record placement 与 member alignment；零宽字段需要例外（MC-042）。 | R2/U2/E1 · MC-042 |
| `F4-T02` `minic_data_layout_effective_field_alignment` — 同上 | R field/natural/cap/explicit alignment→effective field align；普通成员规则不应无条件缩窄 `int:0`。 | R2/U2/E1 · MC-042 |
| `F4-T03` `minic_data_layout_record_depth` — 同上 | R complete Record、Program、目标布局→storage bits/offset/size；共同 consumer 是 sizeof/offsetof/field/Core/global。packed zero-width 证实错 1/2 vs4/5。 | R2/U2/GNU-NATIVE-DIFF · MC-042 |
| `F4-T04` `minic_data_layout_type_depth` — 同上 | R type arena，递归派发 array/record/scalar/pointer、检测上限和 overflow，不涉及机器指令编码。 | R2/U2/E1 |
| `F4-T05` `minic_data_layout_type` — 同上 | R Program + target layout，W caller size/align；Frontend、Core、Target 共用正确的物理类型布局入口。 | R2/U2/E1 |
| `F4-T06` `minic_data_layout_global_object` — 同上 | R Global init/type，W caller extent/align；兼容 tentative incomplete/flexible tail，供 ELF Global plan 与 validator。 | R2/U2/E1 |
| `F4-T07` `minic_data_layout_validate_program` — 同上 | Compiler 发射前的只读布局一致性门；不会自动比较 GNU oracle，MC-042 错布局可通过内部自洽检查。 | R2/U2/E1 |
| `F4-T08` `minic_data_layout_record_field_layout` / `_offset` — 同上 | R Record→field byte/bit offset，Core bit-field、Frontend const-eval、global reloc 共享；不缓存 Record*。 | R2/U2/E1 · MC-042 consumer |
| `F4-T09` `minic_data_layout_global_relocation_offset` — 同上 | R GlobalRelocation/member path，递归解析物理字节位置且界限校验；W caller offset，不分配 owner。 | R2/U2/E1 |
| `F4-T10` `aggregate_scalar_slot_layout_for_object` — 同上 | R aggregate shape，W caller scalar slot cursor/offset；对 array/record/union 逐层计算，不为零大小虚造 slot。 | R2/U2/E1 |
| `F4-T11` `minic_data_layout_global_relocation_target_addend` — 同上 | R relocation constant/member path，目标 emitter 消费 signed addend；越界与 bitfield fail-close。 | R2/U2/E1 |
| `F4-T12` `minic_target_info_sizeof_type` — `target_info.c` | Frontend builtins/const-eval、Core constant facts 调用；GNU void/function sizeof 特许，其他委托共享 DataLayout。 | R2/U2/E1 · MC-042 consumer |
| `F4-T13` `minic_target_info_integer_literal_type` — 同上 | R invocation IntegerModel/base/suffix，尝试 rank 候选而不修改静态 canonical target。 | R2/U2/E1 |
| `F4-T14` `minic_multi_emit_elf_global_section` — `common/global_emit.c` | R global section/fallback，W FILE*；生成 `.bss`/PROGBITS 文本，A64 object dialect 仍待目标 GAS 证据（MC-025）。 | R2/U2/E1 · MC-025 INVESTIGATE |
| `F4-T15` `minic_multi_global_emission_plan_build` — 同上 | R Global+DataLayout→caller POD plan；分类零初始、聚合、重定位，失败不发表独立 heap owner。 | R2/U2/E1 |
| `F4-T16` `minic_multi_emit_global_payload` — 同上 | R plan/object，W FILE*；递归发射 payload、交目标回调发真正 ISA relocation，失败由上层临时 `.s` 事务清理。 | R2/U2/E1 |
| `F4-T17` `minic_multi_emit_simple_global` — 同上 | R global plan，W `.section/.type/.size/.align/payload`；hardcoded `@object` 与 A64 配置差异见 MC-025。 | R2/U2/E1 · MC-025 INVESTIGATE |
| `F4-T18` `minic_multi_direct_visit_global` — `common/direct_program_emit.c` | R target config/Program，W FILE*；extern alias 使用 dialect，而普通定义走上项未传 dialect。 | R2/U2/E1 · MC-025 INVESTIGATE |
| `F4-T19` `minic_multi_direct_write_program` — 同上 | A64/Loong 的 writer，借 Program/Core、构 context/census，委托公共原子 `.s` 输出；无第二套目标文件事务。 | R2/U2/E1 |
| `F4-T20` `minic_program_emit_write` — `common/program_emit.h` | x86/RV/Direct 共用，OWN `mkstemp`/FILE，prefix→globals→functions→finalize→rename，失败删临时文件，旧输出不动。 | R2/U2/E1 |
| `F4-T21` `minic_a64_emit_frame_load` / `_store` / `_address` — `aarch64/frame_emit.c` | R typed FrameRef(region,offset)，借 scratch policy 写 FILE*；大偏移 materialize 避免 scratch alias。 | R2/U2/E1 |
| `F4-T22` `aarch64_emit_prologue` — `aarch64/codegen.c` | R DirectLP64 prepared frame，x29/x30、x19 realign、x8 sret 与 varargs；frame <=1MiB，排除当前 MOVK 立即数伪问题。 | R2/U2/E1 |
| `F4-T23` `minic_aarch64_emit_abi_instruction` — `aarch64/call_emit.c` | R prepared CALL/argument class，W 物理 GPR/FPR/stack/x8；共享 CALL plan 当前最大 64 参数 ×2 slots，排除 `sub sp,#bytes` 超限误报。 | R2/U2/E1 |
| `F4-T24` `minic_x86_64_emit_global` — `x86_64/global_emit.c` | R Common layout/payload，W x86 GAS relocs `.byte/.word/.long/.quad`；失败由公共输出事务回滚。 | R2/U2/E1 |
| `F4-T25` `x86_asm_direct_function_symbol` / `_zero_offset_global_symbol` — `x86_64/machine_asm.c` | R Core SSA 真实 FUNCTION_ADDRESS/GEP+bitcast 身份，给 Linux IRQ/alt/trace 的精确符号；不凭任意 runtime SSA 猜名字。 | R2/U2/E1 |

**明确未完成：** x86 `minic_x86_64_emit_structured_asm` 的约 6,430 行 legacy dispatcher 本轮仅 `R1/U1`（读 typed 顶层和若干 Linux 真实消费者），**不能**把这 25 项外推为该文件所有 ASM forms 已 R2。A64 整体 `asm_contract.c` 同样未逐分支深读。MC-025、MC-027 均保留各自调查状态。

### 8.4 Core CFG / Facts / Reentry（新增复核 24 项）

| ID · 符号与源码 | 真正的 facts/CFG consumer 与所有权/失败规则 | 本轮结果 |
| --- | --- | --- |
| `F4-R01` `minic_core_lower_if` — `core_lower_if.c` | W Core branch blocks，局部 integer facts 经 then/else fork/meet；若条件不可安全省略则保留真实 lowering。 | R2/U2/E1 |
| `F4-R02` `core_local_constants_prepare_if_branch` — 同上 | R known facts，W caller 快照；在 IF split 时隔离各 arm；OOM 走保守失效。 | R2/U2/E1 |
| `F4-R03` `core_local_constants_meet_if_paths` — 同上 | R 两臂 facts，W merge facts 取相同已知值；不使单臂事实泄漏。 | R2/U2/E1 |
| `F4-R04` `core_runtime_if_merge_false_fallthrough` — 同上 | W false/fallthrough CFG 与 facts，恢复通用 IF join 契约。 | R2/U2/E1 |
| `F4-R05` `minic_core_lower_loop_cfg` — `core_lower_loop.c` | W loop head/body/exit blocks，前置修改集扫描；循环入口不能沿用会在迭代中改变的 local facts。 | R2/U2/E1 |
| `F4-R06` `minic_core_lower_loop_statement` — 同上 | W for/while/do-while Core CFG，处理 continue/update 与 break exit，消费 loop 修改集。 | R2/U2/E1 |
| `F4-R07` `core_collect_loop_modified_locals` — 同上 | R AST/Local/ASM side effects，OWN 修改位图；被 loop invariant 证明复用，不是每次独立重算一套事实。 | R2/U2/E1 |
| `F4-R08` `core_restore_loop_invariant_facts` — 同上 | R 修改集，W 恢复入口已知事实的安全子集；不能把循环 body 后事实当成迭代不变。 | R2/U2/E1 |
| `F4-R09` `minic_core_lower_switch` — `core_lower_switch.c` | W case dispatch/nested case blocks；caller `core_lower.c` 在 switch 前后 clear_known，排除“switch 尾部无 meet 即错码”误报。 | R2/U2/E1 |
| `F4-R10` `lower_nested_case_switch` — 同上 | W nested reentry/fallthrough CFG，消费收集过的 inner labels；重复收集是 MC-040 性能债。 | R2/U2/E1 · MC-040 |
| `F4-R11` `lower_switch_case_dispatch` — 同上 | R case label/value，W dispatch CFG；须尊重 fallthrough 与外部 goto reentry。 | R2/U2/E1 |
| `F4-R12` `core_switch_dead_prefix_has_reentry` — 同上 | R source reentry index/label graph，证据不足时保守保留入口，不能按不可达源码强删 case。 | R2/U2/E1 |
| `F4-R13` `minic_core_lower_try_conditional` — `core_lower_expr_flow.c` | W `?:` 两 arm/merge，按 value/effect 类型隔离 facts；Record 特化例外仍是 MC-036。 | R2/U2/E1 · MC-036 对照 |
| `F4-R14` `minic_core_lower_try_sequenced` — 同上 | W 短路/顺序表达式 Core CFG；已由分支事实 fork 控制 RHS 执行语义。 | R2/U2/E1 |
| `F4-R15` `minic_core_lower_try_statement_expression` — 同上 | W GNU `({ ... })` 的 Block/value；source 允许形状受限，不能按一般表达式常量折叠假设有副作用错码。 | R2/U2/E1 |
| `F4-R16` `core_expression_fact_fork_*` — 同上 | R/W per-arm facts snapshot/restore/meet，标量、void `?:` 和逻辑条件均有真实 consumer；记录为函数组，每个路径返回时释放暂态。 | R2/U2/E1 |
| `F4-R17` `minic_core_local_facts_snapshot` — `local_facts.c` | OWN 已知整数/整数域事实快照，供 IF/loop/expr CFG，不返回对原数组的持久借用指针。 | R2/U2/E1 |
| `F4-R18` `minic_core_local_facts_restore` — 同上 | W context known facts，按 snapshot 回滚分支局部状态；失败不能留下虚假常量。 | R2/U2/E1 |
| `F4-R19` `minic_core_local_facts_meet` / `_clear_known` — 同上 | 两路交集和无证明时的全局失效是不同操作；switch/branch/loop 各有 caller。 | R2/U2/E1 |
| `F4-R20` `minic_core_local_fact_invalidate` / `minic_core_local_fact_escape` — 同上 | W 显式修改或逃逸 LocalId 的事实，保守推断不能跨未知间接指针写入。 | R2/U2/E1 |
| `F4-R21` `core_source_collect_expression` / `_block` — `core_lower_local_effects.c` | R AST statement/body/ASM effects，OWN per-loop changed-local set；防止纯值计算掩盖 source-side effect。 | R2/U2/E1 |
| `F4-R22` `core_const_eval_integer_with_locals` — `core_lower_const_facts.c` | R Program + local facts→可证明值；IF 有额外 purity 消费、condition branch 未同形，属于 MC-047 待验证接口风险而非已证错码。 | R2/U2/E1 · MC-047 INVESTIGATE |
| `F4-R23` `core_program_bool_is_provably_zero` — 同上 | R source graph + reentry/cached facts，只有证明安全才给 CFG 优化结论；与另一个更保守的 bool scanner 共享部分遍历。 | R2/U2/E1 · MC-019 |
| `F4-R24` `minic_core_reentry_index_create` / `_query` — `core_reentry_index.c` | OWN 构建用 index，W 统计/查询缓存，OOM 或无结论时回退源全扫描；不能从未构造的 index 推断没有 reentry。 | R2/U2/E1 |

本分片复扫了 `core_lower_if.c`、`core_lower_loop.c`、`core_lower_expr_flow.c`、`local_facts.c`、`core_lower_local_effects.c`、`core_lower_switch.c` 的主路径。另以 8 类合法 GNU CFG fixture 验证 **x86 `-S`→GNU 链接/原生执行与 GCC 一致，8/8 PASS**，RV32/RV64/A64/Loong 对相同 `.i` **仅 `-S` PASS**。尚不能据此关闭 MC-033/036；本轮没有发现独立新 Core wrong-code。

### 8.5 RISC-V：ABI / Wide Integer / Memory / ELF（28 项）

| ID · 符号与源码 | 身份、caller、物理 ABI / 失败边界 | 本轮结果 |
| --- | --- | --- |
| `F4-V01` `rv32_frame_init_with_storage` — `riscv32/rv32_frame.c` | R Core/ABI plan，OWN value/object offsets，过对齐真实帧 `alignDown(E-frame,64)`；变参保存区与 `E` 错位 MC-014。 | R2/U2/ASM-ONLY · MC-014 |
| `F4-V02` `rv32_stack_at` — 同上 | R caller SP/偏移，W target FILE*，大偏移 scratch `$t6` 有安全门；caller stack arg 基址由 caller 当前 SP 保证。 | R2/U2/E1 |
| `F4-V03` `rv32_func` — `riscv32/rv32_emit.c` | OWN frame/save original SP，W prologue/varargs/epilogue；合法 aligned(64) fixture 实际 `.s` 显示 `S+frame` 不等 `E`。 | R2/U2/ASM-ONLY · MC-014 |
| `F4-V04` `rv32_emit_ins` 的 `VARIADIC_ARGUMENT_ADDRESS` — 同上 | R remaining parameter GPR count，W va_list address；0 GPR 子路也用 `S+frame`，独立于跨界错位。 | R2/U2/ASM-ONLY · MC-014 |
| `F4-V05` `rv32_emit_scalar_call_arguments` — `riscv32/rv32_call.c` | R abi slot/arg values，W caller SP+4*stack_slot，先 stack 后 GPR 防后续 materialize 覆写。 | R2/U2/E1 |
| `F4-V06` `rv32_emit_call` — 同上 | R prepared call plan，W direct/indirect CALL/sret pointer；callee 入栈前的 E 是实际 caller outgoing SP。 | R2/U2/E1 |
| `F4-V07` `minic_riscv64_core_frame_initialize_with_storage` — `riscv64/frame_layout.c` | R Core object align/varargs spill，OWN frame offsets；动态 realign 跨界风险 MC-015。 | R2/U2/ASM-ONLY · MC-015 |
| `F4-V08` `riscv64/core_codegen.c` 的 `VARIADIC_ARGUMENT_ADDRESS` | W va_list cursor；有剩余 GPR 时错误从 S 的保存区跨 caller stack，无剩余 GPR 时从保存的 E 取址**正确**。 | R2/U2/ASM-ONLY · MC-015 |
| `F4-V09` `emit_incoming_stack_load64` — `riscv64/call_emit.c` | R incoming argument slot，在动态对齐时读取 `entry_sp_offset` 恢复 E；named stack argument 不是 MC-015 错误来源。 | R2/U2/E1 · 排误报 |
| `F4-V10` `emit_core_call_argument` — 同上 | R outgoing stack slot，W caller SP+8*slot，直接交叉证明 MC-015 跨界之后应读 E。 | R2/U2/E1 |
| `F4-V11` `wide_pair_operands` — `riscv32/rv32_wide_emit.c` | R Core 64-bit operands，返回两个 ILP32 GPR words/Frame slots；拒非法 widths 不作猜测。 | R2/U2/E1 |
| `F4-V12` `wide_shift` — 同上 | R 64-bit RHS 两词，count 高词 guard 后处理 64-bit pair shift；RV32 此路径不能套用 RV64 MC-028。 | R2/U2/E1 |
| `F4-V13` `rv32_emit_wide_integer` — 同上 | W 2-word add/sub/XOR/SAR/compare 和必要 DI libcall；同一合法 GNU C probe RV32 `.s` 通过。 | R2/U2/E1 |
| `F4-V14` `minic_riscv_emit_word_pair_shift` — `riscv/word_operations.c` | R 2×XLEN value/count，W target FILE*，处理高位 shift count、signed SAR，RV32/RV64 真实 caller 共享纯算法。 | R2/U2/E1 |
| `F4-V15` `rv32_scalar_storage_words` — `riscv32/rv32_scalar.c` | R ILP32 type→1/2 words；pointer 为 1、u64 为 2，定义 MC-048 被拒的真实形状。 | R2/U2/E1 · MC-048 |
| `F4-V16` `rv32_emit_integer_instruction` — `riscv32/rv32_integer_emit.c` | R Core cast opcode/from/to words，1→2 仅 integer-extend，`SCALAR_BITCAST` 直接拒，合法 pointer→u64 被错拒（MC-048）。 | R2/U2/LOCAL-COMPILE-DIFF · MC-048 |
| `F4-V17` `minic_core_scalar_bitcast_types_valid` — `core/core_ir_verify.c` | R pointer/int Core types，接受语义上合法的 pointer→integer cast；与 RV32 emitter 的 1→2 word 准入形成已证边界差。 | R2/U2/E1 · MC-048 |
| `F4-V18` `rv32_emit_memory_instruction` — `riscv32/rv32_memory_emit.c` | R typed load/store/width/alignment，packed wide field 使用安全逐字节访存，不盲发未对齐 `lw/sw`。 | R2/U2/E1 |
| `F4-V19` `minic_riscv_emit_typed_memory` — `riscv/typed_memory.h` | R alignment/provenance，W runtime alignment guard 或 byte fallback；普通 C 内存语义与原子 opcode 有明确边界。 | R2/U2/E1 |
| `F4-V20` `minic_riscv_emit_typed_word_pair` — 同上 | 低高两 machine word 顺序一致，scratch 与 address 非别名，供 shared wide memory consumer。 | R2/U2/E1 |
| `F4-V21` `rv32_emit_wide_checked_overflow` — `riscv32/rv32_integer_emit.c` | 64 位 mul overflow 走 `__muldi3`/`__divdi3` 来证溢出，非手写两套乘除；尚无运行态差分。 | R2/U2/E1 |
| `F4-V22` `emit_int128_pair_shift_left` — `riscv64/scalar_emit.c` | R Core RHS type，宽 `__int128` count 被显式 fail-close、64 位 count pass（MC-028）；不称静默错码。 | R2/U2/LOCAL-COMPILE-DIFF · MC-028 |
| `F4-V23` `minic_riscv64_core_scalar_bitcast_supported` — `riscv64/legalize.c` | R source/target width，>8B BITCAST 明确资格拒绝；没发现原生后端读取未初始化高半的静默路径。 | R2/U2/E1 · 排误报 |
| `F4-V24` `minic_riscv64_frame_load_int128` — `riscv64/frame_access.c` | R 128-bit slot + register set，物化高半时保护低半不被 scratch 改写。 | R2/U2/E1 |
| `F4-V25` `minic_riscv64_frame_load_chunk` — 同上 | R partial Record chunk，按字节拼装并避免 caller parameter GPR/scratch 别名。 | R2/U2/E1 |
| `F4-V26` `rv32_reloc` / `minic_riscv64_emit_symbol_value` — `riscv32/rv32_global_emit.c`、`riscv64/global_emit.c` | R symbol+addend，W `.4byte`/`.dword`，合法 `external_blob+13` 生成对应目标 relocation 文本；未有目标 ELF oracle。 | R2/U2/E1 |
| `F4-V27` `emit_strict_i_checked_multiply` — `riscv64/overflow_emit.c` | R Core arithmetic/value width，M 扩展禁用时采用 libgcc multiply+typed store+division 验证，不强行生成非 I 机器指令。 | R2/U2/E1 |
| `F4-V28` `core_structured_inline_asm_supported` — `riscv64/inline_asm_emit.c` | R source GNU ASM operands/roles，Target machine gate 提供准入，未知形态 fail-close；本轮未见独立错码。 | R2/U2/E1 |

本轮只用宿主 GNU C 验源码、MiniC 目标 `.s` 和源码 ABI 关系核对；**没有 RISC-V GNU GAS/ELF/QEMU 的动态验证**。额外矩阵的 wide add/sub/shift/checked-overflow、packed-u64 load/store、`external_blob+13` 两目标 `-S` 均通过，不能外推为运行时正确。MC-014/015 已有合法 C→`.s` 的实物和数学错位证明，但仍分级为 `ASM-ONLY`。

### 8.6 LoongArch64：source/qualified ASM → machine + ABI（45 项）

这部分特别将 `source` 资格与 `_emit` 物理发射拆开：一个允许的 GNU `+r`/`volatile` source form 必须在 Core 和机器端保有完整语义；但 LL/SC、AMO、uaccess 的原生机器内存指令并不是普通 C 输出 store，不能粗暴套用同一未对齐写入 helper。

| ID · 函数（源文件） | source/consumer、输出、失败/别名约束 | 本轮结果 |
| --- | --- | --- |
| `F4-L01` `loong_linux_clock_output_type` — `asm_contract.c` | R output carrier，按 32/64 宽度消去 cv 判资格；`clock_source`/`_emit` 都消费。 | R2/U2/E1 |
| `F4-L02` `loong_linux_clock_form` — 同上 | R 精确 rdtime.d/la.pcrel 形式，源、机器两阶段共享匹配；不允许任意 GNU clock 指令文本。 | R2/U2/E1 |
| `F4-L03` `loong_linux_clock_source` — 同上 | R 结构化 GNU operand/register/clobber role，向 Target qualified ASM gate 交验收。 | R2/U2/E1 |
| `F4-L04` `loong_linux_clock_emit` — 同上 | W 物理时钟指令与 C lvalue，写回走安全 `output_store`；不属于 MC-010。 | R2/U2/E1 |
| `F4-L05` `loong_atomic_scalar_type` — 同上 | R signed/unsigned int/long、cv-normalized width，供不同 AMO 类型资格共同使用。 | R2/U2/E1 |
| `F4-L06` `loong_atomic_qualified_source` — 同上 | R `+ZB` memory/atomic operand，接受的内存效果必须由 Core 带到机器 gate；不能退化普通 C RMW。 | R2/U2/E1 |
| `F4-L07` `minic_loongarch64_emit_amo` — 同上 | W 原子硬件 opcode/.db 与 Core 双操作数结果；须保留一次不可拆原子效果。 | R2/U2/E1 |
| `F4-L08` `loong_atomic_fetch_qualified_source` — 同上 | R `=&r,+ZB,r` 三 slot，Core 推导真实 read/write memory effect。 | R2/U2/E1 |
| `F4-L09` `minic_loongarch64_emit_amo_fetch` — 同上 | W LL/atomic old-value transport，结果 C output 经安全 store；不在 MC-010 的 13 槽内。 | R2/U2/E1 |
| `F4-L10` `loong_linux_cmpxchg_word` — 同上 | R unsigned/signed 32/64 width 与 cv-normalization，被 cmpxchg/xchg/add-unless 双 gate 共享。 | R2/U2/E1 |
| `F4-L11` `loong_linux_cmpxchg_source` — 同上 | R 原 GNU LL/SC 模板及 `%z` width，向 descriptor registry 发表 source 资格。 | R2/U2/E1 |
| `F4-L12` `loong_linux_cmpxchg_emit` — 同上 | W 原子循环和 C output lvalue；某输出直接 `st.w/st.d`，见 MC-010，不能改 LL/SC 本身。 | R2/U2/E1 · MC-010 |
| `F4-L13` `loong_linux_xchg_source` — 同上 | R relaxed/DB/物理寄存器和 memory effect 形态，只有 matching GNU 模板被允许。 | R2/U2/E1 |
| `F4-L14` `loong_linux_xchg_emit` — 同上 | W 原子交换、结果经 safe helper 写回；此普通 XCHG 不属 MC-010 的 small XCHG。 | R2/U2/E1 · 排误报 |
| `F4-L15` `loong_linux_xchg_small_source` — 同上 | R subword xchg mask 和 32-bit LL/SC 形态，不等于 amswap；与机器消费者一一对应。 | R2/U2/E1 |
| `F4-L16` `loong_linux_xchg_small_emit` — 同上 | W 两个逻辑 C output，当前直接 st.w，packed 结果可能不对齐（MC-010）。 | R2/U2/E1 · MC-010 |
| `F4-L17` `loong_percpu_amo_source` — 同上 | R named/numeric 六类 per-CPU 形式，`+ZB` 使 Core 捕获实际内存效果；不能仅因 GNU 文本无 clobber 判错。 | R2/U2/E1 |
| `F4-L18` `loong_percpu_amo_emit` — 同上 | W 原子 per-CPU old-value，result 经 safe output store；没有 MC-010 裸结果写回。 | R2/U2/E1 |
| `F4-L19` `loong_fcsr0_write_source` — 同上 | R movgr2fcsr $fcsr0 精确模板、int/uint/long/ulong 的 cv-normalized 输入资格。 | R2/U2/E1 |
| `F4-L20` `loong_fcsr0_write_emit` — 同上 | R Core source scalar width，W `ld.w/ld.d` + 写 FCSR0 的机器 side effect；不承担 C lvalue output store。 | R2/U2/E1 |
| `F4-L21` `loong_linux_invtlb_source` — 同上 | R exact INVTLB GNU 模板/operand 数目/内存 effect；源语言约束与实码立即数字段分层。 | R2/U2/E1 |
| `F4-L22` `loong_linux_invtlb_emit` — 同上 | R Core 常量立即数，白名单 op=0..6/9..14/16..22，W 物理 INVTLB；非恒定或未支持值 fail-close。 | R2/U2/E1 |
| `F4-L23` `loong_linux_add_unless_source` — 同上 | R 五 operand + `=ZB` 原子 memory effect，允许 C 输出并非要求自然对齐；机器端不安全结果写回见 MC-010。 | R2/U2/E1 · MC-010 |
| `F4-L24` `loong_linux_add_unless_emit` — 同上 | W LL/SC+barrier 和两个 C result；packed `x->rc` 偏移 1 的合法 fixture 实际裸发 `st.w`，MC-010 是 ASM-ONLY。 | R2/U2/ASM-ONLY · MC-010 |
| `F4-L25` `loong_linux_sub_positive_source` — 同上 | R GNU `'I'` 负增量 / `'r'` 正增量的两类模板，结果/内存形状分别认证。 | R2/U2/E1 |
| `F4-L26` `loong_linux_sub_positive_emit` — 同上 | W 依模板选择 add/sub+LL/SC，两个直接结果 store 属 MC-010；不能只把整个原子循环降成 plain C。 | R2/U2/E1 · MC-010 |
| `F4-L27` `loong_linux_futex_cmpxchg_source` — 同上 | R 六 operand、完整双 `__ex_table`，对 `err` 接受 GNU `+r` 的任意 int 初值，未约束为零（MC-045）。 | R2/U2/E1 · MC-045 |
| `F4-L28` `loong_linux_futex_cmpxchg_emit` — 同上 | W fault fixup 与 result；机器实现无条件用 0 改写初值 7 的 `+r` err（MC-045），并有两个裸结果 store（MC-010）。 | R2/U2/ASM-ONLY · MC-045/010 |
| `F4-L29` `loong_linux_futex_rmw_source` — 同上 | R `'0'(0)` 与 `=r(err)` 的 tie/constant，GNU 双 extable；volatile result 在源端被接受而 emitter 比较完整 cv type（MC-037）。 | R2/U2/LOCAL-COMPILE-DIFF · MC-037 |
| `F4-L30` `loong_linux_futex_rmw_emit` — 同上 | R tied-zero SSA 资格，W LL/SC/fault 和两个裸 result；Source/emitter cv 差异 MC-037，不对齐输出 MC-010。 | R2/U2/E1 · MC-037/010 |
| `F4-L31` `loong_linux_uaccess_source` — 同上 | R uaccess faulting load/store 1/2/4/8B、scalar carrier 与 GNU extable 形状；不能把源机器内存效果当新增 C output。 | R2/U2/E1 |
| `F4-L32` `loong_linux_uaccess_emit` — 同上 | W `$t0/$t1` fault scratch，C err/data 走 safe helper；**不是** MC-010 直接未对齐 writeback。 | R2/U2/E1 · 排误报 |
| `F4-L33` `loong_linux_irq_switch_source` — 同上 | R 精确 18 clobber、固定物理 SP/regs GNU 模板；不允许从任意 inline asm 推导 stack switch。 | R2/U2/E1 |
| `F4-L34` `loong_linux_irq_switch_emit` — 同上 | W 独立保存 s0/ra 和切换 SP，切换期间不再访问旧 frame；volatile 普通输入也实际 `-S` pass。 | R2/U2/E1 · 排误报 |
| `F4-L35` `loong_known_address_alignment` — `memory_emit.c` | R OBJECT/global/field/offset DataLayout，能证明时返回对齐，否则保守对齐1；与 `_asm_output_store` 共用。 | R2/U2/E1 |
| `F4-L36` `loong_emit_scalar_store` — 同上 | R width/address/provenance，W FILE*；已证明对齐才 `st.w/st.d`，否则运行时对齐判断或 byte-scatter，MC-010 应复用此处。 | R2/U2/E1 |
| `F4-L37` `emit_integer_memory` — 同上 | R LOAD/STORE/width，W ordinary scalar 字节 fallback；不能对真正硬件原子位宽执行同一拆分。 | R2/U2/E1 |
| `F4-L38` `minic_loongarch64_emit_memory_instruction` — 同上 | R Core LOAD/STORE/ATOMIC_LOAD/ATOMIC_STORE、wide 16B，分派到不同物理内存家族。 | R2/U2/E1 |
| `F4-L39` `emit_lp64_record_chunk_argument` — `call_emit.c` | R aggregate chunk 和 ABI physical slot，1–8B packed byte assembly 不多读尾部半字。 | R2/U2/E1 |
| `F4-L40` `loongarch_builtin_immediate_value` — 同上 | R 最多五层 SSA integer conversion→恒量，CSR 立即数上界独立检查；不接纳任意 runtime GPR。 | R2/U2/E1 |
| `F4-L41` `minic_loongarch64_emit_abi_instruction` — 同上 | R Common LP64 prepared CALL plan，W 8 GPR/8 FPR、wide split、stack 8/16B/sret a0；目标 consumer 不能复制 ABI 规划第二份。 | R2/U2/E1 |
| `F4-L42` `loong_machine_builtin_signature` — `source_policy/loongarch64.c` | R 24 类 GNU `__builtin_loongarch_*` 名、类型、立即数位置→Frontend + Target 双资格消费。 | R2/U2/E1 |
| `F4-L43` `loong_syscall_alias_names_match` — 同上 | R `sys_x` 和 `__se_sys_x` 真实 ELF 名配对，只用于 Linux syscall alias 专项白名单。 | R2/U2/E1 |
| `F4-L44` `loong_syscall_alias_word_pair` — 同上 | R public pointer/≤8B integer 到 sanitizer signed long/longlong 的精确 ABI word 关系，不放宽一般 C prototype。 | R2/U2/E1 |
| `F4-L45` `loong_syscall_alias_abi_compatible` — 同上 | R 两函数签名、定义/asm label/1–6 参数/非变参，TargetAliasAbiPolicy 唯一消费此受控 Linux 特例。 | R2/U2/E1 |

**本轮 Loong 分片证据精确边界：** MC-037 新增 `movfr2gr.d` 和 futex RMW 的 nonvolatile PASS / volatile FAIL；MC-045 用完整 GNU futex CMPXCHG 约束复现 `+r` 的旧值被清 0；MC-010 的 `add_unless` packed result 生成偏移 1 的裸 `st.w`。这三项都有合法 C→MiniC `.s` 证据，但**都没有** LoongArch64 GNU GAS/QEMU 的运行证据。clock、AMO、uaccess、FCSR、per-CPU 和其他 Linux ASM 的安全 helper/资格分工经调用链排除表象重构误报；4096B 过对齐对象被共享 FramePlan 早期拒绝，并未见非法 Loong immediate。

## 第五轮：公共 Target 契约复查（新增）

本节优先记录先前只有模块级摘要、尚无独立函数契约行的生产实现。对象所有权及失败回退已经与 `program_visit`、AArch64/Loong direct、RV32/RV64 frame 和 x86 frame 的实际消费者交叉追踪；`R2/U2/E1` **仅是源码及调用链证据**，不暗示新增目标运行测试。前四轮已列过的 `minic_core_function_plan_build`、`minic_program_emit_write`、`minic_multi_direct_write_program` 在本轮再次复核，但不重复分配新行。

| ID · 函数（源码） | 真实调用/契约、所有权与失败语义 | 核验 |
| --- | --- | --- |
| `F5-M01` `minic_program_function_selected` — `target/common/program_visit.h` | R Function 定义、内部引用状态及选择策略；三个 selection 消费者共用过滤规则，跳过未引用 internal definition 不改变 FunctionId。 | R2/U2/E1 |
| `F5-M02` `minic_program_visit_prepared_functions` — 同上 | R Program/Core/Target，逐定义 OWN 暂态 `MinicCoreFunctionPlan`，借给目标 visit 仅本次 callback 生命周期；无论 ready/visit 结果均 destroy，census 可以继续遍历但不能称文件发射成功。 | R2/U2/E1 |
| `F5-M03` `minic_core_function_facts_analyze_collect` — `target/common/core_function_facts.c` | R Core 全指令，W caller facts、OWN 可选 `va_start` ValueId 数组；需要保存返回地址/动态栈等是 ABI-neutral facts，OOM 清零、释放而不能导致残留半成品。 | R2/U2/E1 |
| `F5-M04` `minic_core_call_view_build` — `target/common/core_call_view.c` | R direct/indirect CALL Core signature，构 borrowed call/argument view；先检查 callee/signature ID、return object/SSA、调用约定及参数区间，不写目标寄存器布局。 | R2/U2/E1 |
| `F5-M05` `minic_core_call_argument_actual_type` — 同上 | R Core VALUE/OBJECT argument，返回准确 source type；Record 只能由 OBJECT 载体提供，VALUE 不能假充 aggregate；不分配。 | R2/U2/E1 |
| `F5-M06` `minic_core_call_view_argument_type` — 同上 | R call view、callee fixed parameter types 和参数 ID，W caller actual type；对固定 Record 做严格身份检查，scalar promotion/ABI class 明确保留给目标。 | R2/U2/E1 |
| `F5-M07` `minic_core_call_index_build` — `target/common/core_call_index.c` | R Core 全指令，OWN ordered CALL site + borrowed view；验证预估 call 数及每个 view，失败 destroy 整份，供 prepared visitor 与独立 frame ABI 消费。 | R2/U2/E1 |
| `F5-M08` `minic_core_call_index_find` — 同上 | R 按 InstructionId 单调排列的已验证索引，binary search 返回 borrowed view/ordinal；不把非 CALL instruction 错当 ordinal。 | R2/U2/E1 |
| `F5-M09` `minic_core_call_index_handle_prepare` — 同上 | 优先 R 借 visitor-owned index；standalone RV32/RV64/A64/Loong consumer 无借用时 OWN 同一布局索引；handle 明确区分 `index` 与 `owned`，不能对 borrowed 调 free。 | R2/U2/E1 |
| `F5-M10` `minic_core_call_index_handle_destroy` — 同上 | OWN 仅释放 handle.owned 和清 borrowed index pointer；无效重复 destroy 安全，生产 consumer 的共享 index 不在此释放。 | R2/U2/E1 |
| `F5-M11` `minic_core_object_storage` — `target/common/core_object_storage.c` | R Object 的 CType、数组 count、显式对齐和 DataLayout，计算字节 extent/align 与溢出门；不涉及任何目标栈槽身份。 | R2/U2/E1 |
| `F5-M12` `minic_core_object_storage_plan_build` — 同上 | R Core 对象表，OWN 单次完整 object layout snapshot；若第 N 项布局不合法则 destroy 已分配表，不发布部分有效的 plan。 | R2/U2/E1 |
| `F5-M13` `minic_core_object_storage_plan_acquire` — 同上 | Common prepared visitor 提供 `shared` 时**浅借只读存储表**，独立消费者则 OWN 本地建表；R/W flag `borrowed` 是 release 的唯一所有权判据。 | R2/U2/E1 |
| `F5-M14` `minic_core_object_storage_plan_release` — 同上 | 借用时只清除本地 plan 视图，拥有时 destroy；已核对 RV32/RV64/x86/LP64 frame 的各处 destructor 使用，没有双重释放的已证反例。 | R2/U2/E1 · 排误报 |
| `F5-M15` `minic_lp64_plan_types` — `target/common/direct_call_plan.c` | R Target+ABI policy+参数类型，W caller POD argument location；A64 WholeOrStack/even pair 与 Loong SplitWithStack/variadic pair 分别由 policy 决定，不能机械归成一个固定 GPR 算法。 | R2/U2/E1 |
| `F5-M16` `minic_lp64_plan_incoming_parameters` — 同上 | R function fixed parameter signature/target ABI；sret 是否消费物理第一个 GPR 由 policy，复用 M15 但不把 hidden pointer 写入 Core 参数数组。 | R2/U2/E1 |
| `F5-M17` `minic_lp64_admit_call` — 同上 | R immutable `MinicCoreCallView`、VALUE/OBJECT 和 return shape；记录是否 indirect record、是否 variadic，严格拒绝 unsupported ABI；只填写暂态 types[64]，不缓存指向栈内类型数组的地址。 | R2/U2/E1 |
| `F5-M18` `minic_lp64_plan_function_calls` — 同上 | 借 semantic CALL index，OWN malloc 的每 CALL physical site；逐站 admit→place→复制实际参数位置，错误 destroy 整份；与 x86/RV 独立 plan 共用 Core call view，而非共享目标 ABI。 | R2/U2/E1 |
| `F5-M19` `minic_lp64_function_call_plan` — 同上 | R prepared cache + CALL index，按 instruction_id 检索并核验 ordinal/site 一致性；返回仅对 cache 生存期有效的 borrowed site。 | R2/U2/E1 |
| `F5-M20` `minic_lp64_call_plan_cache_destroy` — 同上 | OWN 逐站点 free，然后释放 handle 可能持有的独立索引；不会释放 visitor 共享 CALL index。 | R2/U2/E1 |
| `F5-M21` `core_object_address_build_use_index` — `target/common/core_object_intervals.c` | R 全 Core block/terminator，OWN ValueId 索引；分别记录总用途与 LOAD/STORE 直接地址用途，无法证明时放弃复用优化，不硬猜 pointer escape。 | R2/U2/E1 |
| `F5-M22` `core_object_interval_build_index` — 同上 | R 单次指令扫描，OWN object interval/disqualify 表；record load、CALL object 和 return object 排除共槽，解析不完整或 OOM 返回不可用而非重启旧 O(objects × IR) 全扫。 | R2/U2/E1 |
| `F5-M23` `core_scalar_object_first_access_defines` — 同上 | R 候选唯一 owner block 的 OBJECT_ADDRESS/LOAD/STORE/参数初始化，OWN 暂态 ValueId producer position 索引；第一真实读取前须定义，OOM 时 `false`，从不授予未证实的跨块共享。 | R2/U2/E1 |
| `F5-M24` `minic_core_object_interval` — 同上 | R 单个 Object 的类型、尺寸、对齐、地址用途；只有宽度≤物理 unit 且全局索引已建立的候选可复用，其他返回成功但 `reusable=false`，与整个 Frame 失败分开。 | R2/U2/E1 |
| `F5-M25` `minic_core_object_reuse_plan_build` — `target/common/core_object_reuse.c` | R scalar interval proof，OWN slot-array 和 intervals；按已证明的区间冲突逐个分配 slot，失败清整份计划。当前是保守 O(objects×slots×prior) 配色，**性能成本另审，不是已证 wrong-code**。 | R2/U2/E1 |
| `F5-M26` `minic_core_object_reuse_plan_get` / `_destroy` — 同上 | getter 借 prepared plan，返回对象是否 reusable/slot/peak；destroy 释放分析索引与全部 owned 表。RV32/RV64 frame 真正消费该计划，其他目标不得虚称已启用。 | R2/U2/E1 |
| `F5-M27` `minic_multi_direct_machine_instruction_shape` — `target/common/direct_instruction_semantics.c` | R Core opcode/value kind，W caller machine family/result slot；对 ASM/effect-only 不虚造 SSA，A64/Loong 机器 emitter 仍须各自再次认证机器编码。 | R2/U2/E1 |
| `F5-M28` `minic_multi_direct_record_copy_extent` / `_record_load_extent` — 同上 | R Record identity、source address、volatile 和 DataLayout，返回 byte extent及 frame destination；以 16 MiB 限制阻止无界 copy，而不在 Core 写死目标小指令展开上限。 | R2/U2/E1 |

**第五轮 Common 分片结论：** `CoreFunctionPlan` 是五目标共用的一次性 ABI-neutral owner；A64/Loong `LP64` 同处只表示共享**策略化调用位置规划**，不表示两 ISA 共用同一物理指令生成或完全相同的 ABI。RV32/RV64 object reuse 的旧全扫描已无独立慢路径消费者，而 Compiler 的 legacy inline fallback 仍是另一项真实语义。当前没有依据把这些活跃保守实现直接删掉。

### 第五轮 Compiler / Driver / Target registration（30 个函数）

这些函数在本轮按 caller→callee→失败出口复核；`minic-cc` 五目标直接发射 `.s` 的能力，不能和历史 RV64 staged driver 的链接能力混为一谈。以下只有 `R2/U2/E1` 源码证据，不等于新一轮目标运行。

| ID · 函数（源码） | 调用关系、owner 与失败契约 | 核验 |
| --- | --- | --- |
| `F5-C01` `minic_bootstrap_trace_enabled` — `compiler/compiler.c` | R env，按变量存在性启用，不解析 `0`；trace-stage 唯一消费。 | R2/U2/E1 |
| `F5-C02` `minic_bootstrap_trace_stage` — 同上 | R path/stage，W stderr，不改变 Program，Compiler 五个阶段调用。 | R2/U2/E1 |
| `F5-C03` `minic_set_diagnostic` — 同上 | R format/path，W caller Diagnostic 内联 buffer，不保留借用字符串；file/verify/compile 错误共享。 | R2/U2/E1 |
| `F5-C04` `minic_validate_core_functions` — 同上 | R CoreSet status，W Diagnostic；census 可多报错，但返回仍是 false，不能掩盖 lower 失败。 | R2/U2/E1 |
| `F5-C05` `minic_read_file` — 同上 | OWN FILE/SourceBuffer，读/分配失败本地清理，成功 XFER buffer 给 Compiler 的统一退出。 | R2/U2/E1 |
| `F5-C06` `minic_set_ast_verify_diagnostic` — 同上 | R verifier stage/index，W Diagnostic；非法下标/NULL detail 降级成安全错误信息。 | R2/U2/E1 |
| `F5-C07` `minic_compile_preprocessed_file_for_target` — 同上 | OWN SourceBuffer/Program/CoreSet，R 调用方 invocation Target；Parse→Normalize→Inline→Core→Validate→Backend，失败共同析构。 | R2/U2/E1 |
| `F5-C08` `minic_compile_preprocessed_file_with_options` — 同上 | 历史 RV64 default target 包装 C07，透传 function_sections，非第二套 compiler pipeline。 | R2/U2/E1 · 排误报 |
| `F5-C09` `minic_compile_preprocessed_file` — 同上 | 历史 wrapper→C08(false)→C07，无新增资源所有权。 | R2/U2/E1 |
| `F5-C10` `minic_core_set_bootstrap_trace_enabled` — `compiler/core_function_set.c` | R env，供 CoreSet begin/end 诊断，不改变 Core。 | R2/U2/E1 |
| `F5-C11` `minic_core_function_set_initialize` — 同上 | W caller 新/已释放 CoreSet 归零；不可用来释放 live owner。 | R2/U2/E1 |
| `F5-C12` `minic_core_function_set_destroy` — 同上 | OWN 逐槽 CoreFunction 与顶层 functions/status；LOWER_ERROR 槽也可释放。 | R2/U2/E1 |
| `F5-C13` `minic_prepare_core_function_set` — 同上 | OWN 暂态 CoreSet+可选 reentry index；构造失败保留旧 output，成功整体 XFER；单函数 lower 结果交 C04 验证。 | R2/U2/E1 |
| `F5-C14` `minic_backend_usage_group` — `target/backend_core_usage.c` | R Core opcode→census 诊断分类；dump 消费，不是 ISA 可发射性判定。 | R2/U2/E1 |
| `F5-C15` `minic_backend_usage_bits` — 同上 | R Core result/argument + target type width；未知宽度回报 unknown，未证明机器支持。 | R2/U2/E1 |
| `F5-C16` `minic_backend_core_usage_dump` — 同上 | R CoreSet 全函数 Core 指令，输出分组统计到诊断流；不修改 IR，也不替代合法性验证。 | R2/U2/E1 |
| `F5-C17` `minic_target_machine_options_resolve` — `target/target_machine_options.c` | R Target + CLI machine flags；W caller resolved options，拒目标未定义扩展，source/codegen ISA 分离。 | R2/U2/E1 |
| `F5-C18` `minic_print_usage` — `tools/minic-cc/main.c` | R target catalog，打印已注册用法，无编译资源所有权。 | R2/U2/E1 |
| `F5-C19` `main` — 同上 | OWN CLI path/Target invocation；从选项进入唯一 Compiler Pipeline，错误退出与信息打印分开。 | R2/U2/E1 |
| `F5-C20` `minic_target_catalog_all` — `target/target_catalog.c` | R static Target registration，只借条目，调用方不得释放。 | R2/U2/E1 |
| `F5-C21` `minic_target_catalog_find` — 同上 | R target name，返回已注册 borrowed descriptor，未知目标 fail closed。 | R2/U2/E1 |
| `F5-C22` `minic_target_catalog_for_architecture` — 同上 | R ArchitectureId，选择唯一注册目标；不将机器实现写进前端。 | R2/U2/E1 |
| `F5-C23` `minic_target_catalog_default` — 同上 | R catalog 默认入口；用于兼容历史 target 选择，不与独立 ABI 混同。 | R2/U2/E1 |
| `F5-C24` `minic_target_info_for_architecture` — `target/target_info.c` | R registered per-target DataLayout/ABI/ASM policy；不是 Compiler 架构 switch。 | R2/U2/E1 |
| `F5-C25` `minic_target_invocation_begin` — 同上 | W caller invocation，绑定目标快照和 machine options 生命周期。 | R2/U2/E1 |
| `F5-C26` `minic_target_invocation_plain_char` — 同上 | R invocation TargetInfo，返回 C plain char signedness，不写 Program。 | R2/U2/E1 |
| `F5-C27` `minic_target_invocation_short_wchar` — 同上 | R target wchar policy，决定短 wchar 类型语义，与 C parser/布局契约衔接。 | R2/U2/E1 |
| `F5-C28` `minic_backend_for_target` — `target/backend.c` | R registered target，返回 target-specific emit callback，未知描述符拒绝；不复制通用 Core 算法。 | R2/U2/E1 |
| `F5-C29` `minic_backend_emit_assembly` — 同上 | R verified CoreSet + immutable Program/Target，执行后端发射；失败由 caller 回收 Core owner。 | R2/U2/E1 |
| `F5-C30` `minic_program_emit_write` — `target/common/program_emit.c` | OWN 临时输出文件/事务式写入；最终 rename 仅在完整发射成功后发生；多个目标复用相同提交协议。 | R2/U2/E1 |

### 第五轮 Frontend：静态初始化器、枚举、属性与 Record（30 组）

下列 `F5-F01..F30` 按函数组/生产入口计数；每行均追过真实 consumer 与失败清理。已确认问题的 ASan 证据单独在 MC 台账登记，不能由一个反例给整份前端模块提升运行证据等级。

| ID · 函数（`src/frontend/`） | 契约、所有权与实际 consumer | 核验 |
| --- | --- | --- |
| `F5-F01` `initializer.c:grow_actions/plan_initialize/destroy` | OWN plan.actions；realloc 不改变其他 owner，失败不发布数量，destroy 释放整个计划。 | R2/U2/E1 |
| `F5-F02` `initializer.c:add_action/add_positional/add_designated` | W plan；只在追加成功后更新 extent、最后写、next_index；static/runtime parser 同用。 | R2/U2/E1 |
| `F5-F03` `initializer.c:initializer_find_unclaimed` | R resolved owners，W 自有 successor path compression；逆序解析 designated 覆盖。 | R2/U2/E1 |
| `F5-F04` `initializer.c:plan_resolve/resolved_destroy` | OWN owners/successor/final counts/first-owned；static/runtime parser 消费，失败释放部分分配。 | R2/U2/E1 |
| `F5-F05` `initializer.c:plan_element_count` | R 显式/推断数组 extent；无外逃 borrowed pointer。 | R2/U2/E1 |
| `F5-F06` `parser_enum.c:create_named_enum/get_or_create_enum_tag` | OWN Program Enum entity，Parser scoped tag identity；部分失败实体由 Program 析构。 | R2/U2/E1 |
| `F5-F07` `parser_enum.c:enum_name_hash/global_slot_insert/global_index_rebuild/sync` | OWN Parser 全局枚举名 cache；分配成功再替换，OOM 退回线性搜索。 | R2/U2/E1 |
| `F5-F08` `parser_enum.c:global_linear_fallback/find_enum_constant` | R 局部 enum binding + 全局 index；probe owner 不匹配时线性退回。 | R2/U2/E1 |
| `F5-F09` `parser_enum.c:bind_enum_constant/destroy_enum_constants` | OWN Parser 动态 binding 表，不拥有 Program EnumeratorId；attribute probe 浅拷贝会导致 MC-005。 | R2/U2/E1 · MC-005 |
| `F5-F10` `parser_enum.c:signed_type_fits/unsigned_type_fits/enum_value_type/normalize_value_bits/const_value_to_numeric/parse_enum_integer_value/next_enum_numeric` | R Target 整数宽度，值型规范化并判 overflow；没有跨 arena 借用。 | R2/U2/E1 |
| `F5-F11` `parser_enum.c:choose_enum_compatible_type/choose_packed_enum_compatible_type/enum_mode_argument_is_byte/consume_enum_attribute` | R enum 候选类型及 GNU packed/mode；按 TargetInfo 选择，未知 mode 拒绝。 | R2/U2/E1 |
| `F5-F12` `parser_enum.c:parse_enum_specifier/parse_enum_definition` | OWN Program Enum/Enumerator IDs；所有元素写完再选择兼容底层类型。 | R2/U2/E1 |
| `F5-F13` `parser_attribute.c:parser_token_text_is/attribute_descriptor_for_token/validate_attribute_argument_count/parse_attribute_arguments` | R 稳定 SourceSpan、临时栈解析；语法层校验括号和参数数量。 | R2/U2/E1 |
| `F5-F14` `parser_attribute.c:parse_gnu_attribute_lists/collect_parsed_attribute/collect_gnu_attribute_lists` | W caller 属性 callback/out 数组，60+ parser consumer；失败不保证整体语义回滚。 | R2/U2/E1 |
| `F5-F15` `parser_attribute.c:consume_function_pointer_attribute/apply_function_pointer_declarator_attributes` | R target calling convention policy；函数指针 TypeId 转换交 Program owner。 | R2/U2/E1 |
| `F5-F16` `parser_attribute.c:apply_section_attribute` | R SourceSpan，检查长度/冲突后一次写 caller char buffer，不存悬空文本地址。 | R2/U2/E1 |
| `F5-F17` `parser_attribute.c:parse_object_visibility_argument/apply_object_visibility_attribute` | 识别四种 visibility，W caller object metadata，拒冲突值。 | R2/U2/E1 |
| `F5-F18` `parser_attribute.c:resolve_object_alias_attribute/consume_object_attribute` | R Program globals IDs，解析 alias/SECTION/WEAK/ALIGNED 等，未定义 target fail closed。 | R2/U2/E1 |
| `F5-F19` `parser_attribute.c:init_object_attribute_context/parse_object_attribute_lists/apply_object_attribute_lists` | W caller 栈 out-parameters；无拥有的 heap，外层 global parser 消费。 | R2/U2/E1 |
| `F5-F20` `parser_attribute.c:apply_alignment_attribute` | `probe=*parser` 语义解析会扩容 tags/enum_constants，浅拷贝破坏 owner（MC-004/005）。 | R2/U2/ASAN |
| `F5-F21` `parser_record.c:record_has_field/record_field_starts_parenthesized_pointer_array/_object/_attributed_function_pointer` | 词法 lookahead 的浅复制只读 token；不是 MC-004 的有副作用语义 probe。 | R2/U2/E1 |
| `F5-F22` `parser_record.c:parse_pointer_to_array_field_declarator/parse_function_pointer_field_declarator` | R type IDs/ABI 属性，构 pointer type；字段名 SourceSpan 借稳定 SourceBuffer。 | R2/U2/E1 |
| `F5-F23` `parser_record.c:consume_record_field_attribute/parse_typed_record_field_attributes/apply_record_field_declaration_attributes` | W caller 临时字段属性，布局语义经 DataLayout，错误时不发布半成品。 | R2/U2/E1 |
| `F5-F24` `parser_record.c:parse_record_bit_field_width` | R 常量求值与 Target 字段宽度，拒负值/超宽/具名零宽，匿名零宽合法。 | R2/U2/E1 |
| `F5-F25` `parser_record.c:parse_record_field_declarator` | 借 Record arena 只在无扩容区间使用；字段追加后按 RecordId 重取，再写 metadata；失败部分实体由 Program owner 清理。 | R2/U2/E1 |
| `F5-F26` `parser_record.c:consume_record_type_attribute/parse_record_type_attributes/parse_record_field` | W Record 类型属性；解析嵌套类型后重新取 ID，属性语义由 TypeInfo 决定。 | R2/U2/E1 |
| `F5-F27` `parser_record.c:parse_record_definition_specifier/parse_record_forward_declaration/parse_record_definition` | W Program record arena，跨递归用 RecordId；TagBinding* 只借到 append 前。 | R2/U2/E1 |
| `F5-F28` `parser_static_initializer.c:capture_static_aggregate_array_action/materialize_static_aggregate_array_action` | OWN 独立 heap 中的 initializer/relocs/action，反向 rebasing 后归还 GlobalObject，失败统一 destroy。 | R2/U2/E1 |
| `F5-F29` `parser_static_initializer.c:append_static_chained_array_designator_value` | `ArrayType*` 跨子表达式 `sizeof(int[3])` 重分配后继续读，MC-049 ASan UAF。 | R2/U2/ASAN · MC-049 |
| `F5-F30` `parser_static_initializer.c:overwrite_static_zero_record_value/overwrite_static_zero_record_constant` | `Record*` 跨递归增长后读取旧 fields，MC-023 已升为 ASan 确认。 | R2/U2/ASAN · MC-023 |

### 第五轮 Core：Scalar、Record、Verifier、ASM（30 组）

| ID · 函数（`src/core/`） | 契约、生命周期与真实 consumer | 核验 |
| --- | --- | --- |
| `F5-IR01` `core_lower_scalar_values.c:minic_core_lower_internal_append_integer_conversion` | R Value.type，W typed INTEGER_CONVERSION；Verifier→machine scalar；按 Target DataLayout 执行宽度语义。 | R2/U2/E1 |
| `F5-IR02` `...:append_scalar_bitcast` | R 标量类型，W SCALAR_BITCAST；Verifier 限 pointer/int 形状，record/va_arg 与相等性路径消费。 | R2/U2/E1 |
| `F5-IR03` `...:minic_core_lower_internal_emit_scalar_load` | R address ValueId，W LOAD（保留 volatile 标志），assignments/record 消费。 | R2/U2/E1 |
| `F5-IR04` `...:minic_core_lower_internal_emit_scalar_store` | R address/typed value，W STORE effect 指令，assignments/spill 消费。 | R2/U2/E1 |
| `F5-IR05` `...:core_scalar_object_address` | R stable ObjectId，W OBJECT_ADDRESS ValueId；机器 frame 地址由 Target 负责。 | R2/U2/E1 |
| `F5-IR06` `...:spill_scalar_value` | OWN Core 临时 object，写 store，解决 RHS/CFG 中间值生命周期。 | R2/U2/E1 |
| `F5-IR07` `...:reload_scalar_value` | R ObjectId，W 新 SSA LOAD，避免跨块非法直接复用旧 ValueId。 | R2/U2/E1 |
| `F5-IR08` `...:lower_scalar_equality_operands` | R LHS/RHS C 类型，先 spill LHS 再 RHS/reload；避免右侧求值污染事实/块。 | R2/U2/E1 |
| `F5-IR09` `...:lower_integer_binary_operands` | R common integer type，W 两个 typed snapshot，arith/scalar compare 消费。 | R2/U2/E1 |
| `F5-IR10` `...:lower_floating_binary_operands` | R float/double 源表达式，W FLOAT/DOUBLE operands，long double 不走该一般路径。 | R2/U2/E1 |
| `F5-IR11` `...:append_runtime_scalar_call` | OWN Core CALL callee、签名和 arguments，x86 x87 intercept 或目标 TF ABI libcall 消费。 | R2/U2/E1 |
| `F5-IR12` `...:append_long_double_conversion` | R 转换源/目标类型，W TF runtime helper；有真实 caller，不能作为旧逻辑删除。 | R2/U2/E1 |
| `F5-IR13` `...:append_long_double_binary_helper` | R 两个 long-double 值，W `__*tf3` CALL；负零问题出于 unary 调用方式，非 helper 本身。 | R2/U2/E1 · MC-051 |
| `F5-IR14` `...:lower_scalar_assignment_value` | R 源表达式/type，W conversion、bool zero test、TF bridge；赋值和调用共用。 | R2/U2/E1 |
| `F5-IR15` `core_lower_arithmetic.c:minic_core_lower_try_arithmetic_unary` | float/double 各有精确 NEGATE；long double 用 `__subtf3(+0,x)`，已原生差分确认负零丢失。 | R2/U2/GNU-NATIVE-DIFF · MC-051 |
| `F5-IR16` `...:minic_core_lower_try_arithmetic_binary` | R operator/promoted type，W integer/FP opcode、TF helper；Core expression dispatcher 消费。 | R2/U2/E1 |
| `F5-IR17` `...:minic_core_lower_try_arithmetic_compound` | Lvalue address 和 RHS snapshot 分别持有，最后 STORE；与一般 binary 不可盲目合并。 | R2/U2/E1 |
| `F5-IR18` `core_lower_record.c:minic_core_lower_record_compound_literal_object` | R frontend local backing+initializer，OWN Core ObjectId；record_materialized_address 消费。 | R2/U2/E1 |
| `F5-IR19` `...:lower_record_value_address` | R record lvalue/statement expression，OWN RHS snapshot，按 C 求值时序写 RECORD_COPY。 | R2/U2/E1 |
| `F5-IR20` `...:lower_abi_cursor_scalar_record_va_arg` | R 单字段无 padding/bitfield Record + Target ABI cursor，materialize 到对象；其他形状 fail closed。 | R2/U2/E1 |
| `F5-IR21` `...:lower_record_va_arg_object` | R `ABI_CURSOR`/`LINEAR_SLOT` Target property，OWN aggregate result，更新 va_list。 | R2/U2/E1 |
| `F5-IR22` `...:record_materialized_address` | 统一 conditional/member/va_arg/compound/call 聚合来源的地址身份，提供调用与 COPY consumer。 | R2/U2/E1 |
| `F5-IR23` `...:minic_core_lower_record_conditional_object` | OWN result object + CFG 分块；局部事实缺 fork/meet 已在 MC-036 证明，不重复发新编号。 | R2/U2/E1 · MC-036 |
| `F5-IR24` `...:record_load_object` | R typed source，OWN aggregate snapshot，W RECORD_LOAD 与 volatile；Target record load 消费。 | R2/U2/E1 |
| `F5-IR25` `...:record_copy_statement` | 先求 source 再求 destination，W RECORD_COPY；未见独立 volatile bit 但机器 copy 特化，需更多证据才判错。 | R2/U2/E1 · INVESTIGATING |
| `F5-IR26` `core_ir_verify.c:instruction_is_valid` | R 所有 opcode typed shape、ValueId/OBJECT/ASM/call role；合法 C 的 indirect aggregate vararg 被明确拒绝。 | R2/U2/E1 |
| `F5-IR27` `...:verify_block/minic_core_function_verify` | R Core definitions；ValueId 全函数唯一、可用性按 block 重置；fixed binding/enum storage shape 缺口另调查。 | R2/U2/E1 · MC-055 |
| `F5-IR28` `value_uses.c:minic_core_visit_instruction_value_uses` / terminator | 按 opcode 区分 VALUE/OBJECT/CALL/ASM/CFG uses；Target spill layout、object interval consumer 已交叉核对。 | R2/U2/E1 |
| `F5-IR29` `core_lower_asm_operands.c:core_asm_publish_operand_plan/core_inline_asm_numeric_template` | R qualified source operand roles，W Core numeric template/tied/effect metadata，保留 `%%/%10/%l[name]` 特例。 | R2/U2/E1 |
| `F5-IR30` `...:core_inline_asm_immediate_text/core_inline_asm_specialize_immediates` / output variant | OWN 重写后 template（caller free）；R const/local facts/Target qualifier，输出 variant 不能与立即量 variant 合并。 | R2/U2/E1 |

**Core 待调查契约：** `minic_core_function_verify` 对 `fixed_register_bindings` 和 `enum_types` 未做与其他 arena 相同的 storage-shape preflight；真实 Parser→Core 生产链目前维持容量不变式，尚无有效 C 反例。该风险先以 MC-055 `INVESTIGATING` 跟踪，不能当成现存 wrong-code。

### 第五轮 RISC-V：RV32 宽标量与 RV64 LP64 Softfloat（30 个）

| ID · 函数（`src/target/`） | 调用、所有权、失败/ABI 契约 | 核验 |
| --- | --- | --- |
| `F5-R01` `riscv32/rv32_fp_emit.c:rv32_emit_fp_instruction` | `rv32_emit`→binary32/64 比较/转换 libcall；RV32I 10 个合法 C probe `-S` 成功。 | R2/U2/E1 |
| `F5-R02` `riscv32/rv32_wide_emit.c:rv32_is_wide_integer` | R integer width，区分 RV32 两字 long long 与不支持的 i128。 | R2/U2/E1 |
| `F5-R03` `...:wide_pair_operands` | R Core operands，W t0/t1+t2/t3 physical words，算术/比较消费者共用。 | R2/U2/E1 |
| `F5-R04` `...:wide_shift` | R shift count，单/双 word 分路移位，不将 long long 降为单字。 | R2/U2/E1 |
| `F5-R05` `...:rv32_emit_wide_integer` | R 64-bit Core arithmetic，W pair/shift/libcall；RV32 emitter 真实调用。 | R2/U2/E1 |
| `F5-R06` `riscv32/rv32_scalar.c:rv32_scalar_storage_words` | R scalar type，double/long long 取两字，i128 fail closed；ABI/FP consumer 共用。 | R2/U2/E1 |
| `F5-R07` `...:rv32_scalar` | R scalar signedness/width，供整数 emitter 和 ABI 参数分类。 | R2/U2/E1 |
| `F5-R08` `...:rv32_canon` | W 1/8/16-bit 规范扩展值，CALL/FP consumer 真实使用。 | R2/U2/E1 |
| `F5-R09` `...:rv32_load_mem` | R typed memory address/alignment，W scalar GPR，交给 memory emit。 | R2/U2/E1 |
| `F5-R10` `...:rv32_store_mem` | R typed value/address/alignment，W memory，和 LOAD 对称但有副作用。 | R2/U2/E1 |
| `F5-R11` `...:rv32_load2` | R 两个 SSA ValueId，W 两物理 GPR，wide arithmetic consumer。 | R2/U2/E1 |
| `F5-R12` `riscv64/fp_emit.c:rv64_emit_fp_binary` | R Core floating binary + ISA，LP64 软 helper 或 LP64D 原生 F/D；四则是实际支持的子集。 | R2/U2/E1 |
| `F5-R13` `...:minic_riscv64_emit_fp_instruction` | R FP opcode，W GPR/FPR 指令，LP64 10 个非四则操作没有软 lowering；MC-053。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F5-R14` `riscv64/runtime_libcall.c:minic_riscv64_uses_arithmetic_libcall` | R opcode/ISA，标记需 helper 的算术，overflow/return-address 规划消费。 | R2/U2/E1 |
| `F5-R15` `...:rv64_arithmetic_kind` | R Core op，映射 5 个整数 runtime arithmetic helper 类型。 | R2/U2/E1 |
| `F5-R16` `...:minic_riscv64_softfloat_binary_symbol` | R F32/F64 四则，返回 helper 名；比较/转换无映射是 MC-053 能力缺口。 | R2/U2/E1 · MC-053 |
| `F5-R17` `...:minic_riscv64_needs_arithmetic_libcall` | R ISA+Core，W caller RA/preflight 需求；softfloat/integer/strict-I checked multiply 一起判。 | R2/U2/E1 |
| `F5-R18` `...:minic_riscv64_emit_arithmetic_libcall` | R validated arguments/ABI，W helper CALL；不能直接以机器 F/D opcode 替代 LP64。 | R2/U2/E1 |
| `F5-R19` `riscv64/overflow_emit.c:emit_strict_i_checked_multiply` | R signed multiplication inputs，W overflow result，处理 MIN/-1；M 扩展与 helper 选择分开。 | R2/U2/E1 |
| `F5-R20` `...:minic_riscv64_emit_overflow_instruction` | R Core result-width/ISA，选择 strict-I libcall 或 native-M。 | R2/U2/E1 |
| `F5-R21` `riscv64/function_abi_plan.c:minic_riscv64_function_abi_plan_qualified` | R profile/params/returns，W caller ABI GPR/FPR/stack placements。 | R2/U2/E1 |
| `F5-R22` `...:minic_riscv64_function_abi_plan_build_profile` | R ISA-resolved ABI profile，OWN complete plan，Core admission consumer。 | R2/U2/E1 |
| `F5-R23` `...:minic_riscv64_function_abi_plan_build` | standalone frame fallback 默认 lp64d，不是 production LP64 的第二 ABI。 | R2/U2/E1 · 排误报 |
| `F5-R24` `...:minic_riscv64_function_abi_plan_destroy` | OWN plan resources；admission/frame/core codegen 的清理出口共用。 | R2/U2/E1 |
| `F5-R25` `riscv64/legalize.c:minic_riscv64_scalar_type` | R wide/long double scalar shape，machine admission 与 call consume。 | R2/U2/E1 |
| `F5-R26` `...:rv64_wide_call_argument_supported` | R 128-bit argument，校验两 GPR 或 stack 位置。 | R2/U2/E1 |
| `F5-R27` `...:rv64_scalar_gpr_call_argument_supported` | R LP64 softfloat scalar/GPR fallback，防把 LP64D ABI 套进 LP64。 | R2/U2/E1 |
| `F5-R28` `...:rv64_core_call_arguments_supported` | R direct/indirect calls/prepared ABI sites；indirect record vararg 明确 fail closed。 | R2/U2/E1 |
| `F5-R29` `...:rv64_core_call_return_supported` | R return type/object value，验证 void/int/float/TI/record return slot。 | R2/U2/E1 |
| `F5-R30` `riscv64/isa.c:minic_riscv_isa_admit_core_instruction` | 严格预发射 ISA gate；LP64 软四则允许，其他浮点比较/转换拒绝并显示占位 F/D 诊断（MC-053）。 | R2/U2/LOCAL-COMPILE-DIFF |

**RV 边界补证：** RV64I `__int128` 运算 9/9 `.s`、binary128 long double 9/9 `.s`；RV32I 对 i128 明确拒绝。RV64IM 的 i128 MUL 为本地 `mul/mulhu`，不能按“未存 ra”错报 libcall bug。以上并不证明目标 ELF/QEMU 已执行。

### 第五轮 AArch64：Linux GNU ASM Source→Core→Machine（30 个）

总入口 `minic_aarch64_qualify_inline_asm`→`minic_backend_capture_asm_operand_source`→Core structured ASM→`minic_aarch64_emit_qualified_structured_asm`；以下 Source 函数读取原始 GNU operand/type 形状，Emit 函数读取目标 Core ABI/Frame 后写 `.s`，两者不共享可变缓冲区。

| ID · 函数（`src/target/aarch64/asm_contract.c`） | 资格/指令写回契约 | 核验 |
| --- | --- | --- |
| `F5-A01` `aarch64_linux_zeropad_source` | `=&r` ulong、r pointer、Q memory，隐式内存、GNU exception template 全形状检查。 | R2/U2/E1 |
| `F5-A02` `aarch64_linux_zeropad_emit` | x9/x10/x11 定位，12B `__ex_table` PREL32+类型4，输出8B。 | R2/U2/E1 |
| `F5-A03` `aarch64_linux_uaccess_source` | LDTR/STTR 1/2/4/8，`+r` error、`=r/rZ` value、r pointer；缺输出对象与 ISA 物理宽度的一致性门控。 | R2/U2/ASM-ONLY · MC-052 |
| `F5-A04` `aarch64_linux_uaccess_emit` | LDTR 8/16/32-bit 结果仍以 `str x10,[x12]` 写8B；packed/u8/u16/u32 可被覆盖。 | R2/U2/ASM-ONLY · MC-052 |
| `F5-A05` `aarch64_linux_xpaclri_source` | x30 固定 local +r ulong，hint#7 单个 RW role。 | R2/U2/E1 |
| `F5-A06` `aarch64_linux_xpaclri_emit` | x30 保存/恢复与 hint#7，维持函数 LR frame。 | R2/U2/E1 |
| `F5-A07` `aarch64_linux_smccc_source` | hvc/smc #0，x0..x3 输出和 x0..x7 输入固定约束，memory clobber。 | R2/U2/E1 |
| `F5-A08` `aarch64_linux_smccc_emit` | stage x0..x7→trap→x0..x3 写回。 | R2/U2/E1 |
| `F5-A09` `aarch64_linux_svc_source` | svc#0，x0 result、x0/x1/x8 inputs，long 型参数和隐式 effect。 | R2/U2/E1 |
| `F5-A10` `aarch64_linux_svc_emit` | 固定 x0/x1/x8 stage/回写；源端 r0 别名与机端 x0 要求仍需 alias 复核，非确认错码。 | R2/U2/E1 |
| `F5-A11` `aarch64_linux_wfxt_source` | WFIT/WFET sysreg 两个常量形状，单64位 scalar。 | R2/U2/E1 |
| `F5-A12` `aarch64_linux_wfxt_emit` | x9 timeout→`msr`，机器端复核完整 template。 | R2/U2/E1 |
| `F5-A13` `aarch64_linux_atomic_dec_positive_source` | 64b `=&r`×2 与 `+Q`，CC+memory。 | R2/U2/E1 |
| `F5-A14` `aarch64_linux_atomic_dec_positive_emit` | ldxr/subs/stlxr retry+dmb；两个 64b 输出正确写回。 | R2/U2/E1 |
| `F5-A15` `aarch64_linux_cmpxchg128_source` | 128-bit `+Q`、状态/结果及四个u64输入，总8roles。 | R2/U2/E1 |
| `F5-A16` `aarch64_linux_cmpxchg128_emit` | ldxp/ccmp/stxp/stlxp，pair旧值与32位状态按物理宽度写回。 | R2/U2/E1 |
| `F5-A17` `aarch64_linux_lse_casp128_source` | CASP/PAL x0/x1 pair、x2/x3输入，+Q与多寄存器约束。 | R2/U2/E1 |
| `F5-A18` `aarch64_linux_lse_casp128_emit` | `.arch_extension lse`、casp/caspal、两条 x0/x1 旧值写回；未用附加 source operand 不等于自动错码。 | R2/U2/E1 |
| `F5-A19` `aarch64_linux_lse_dec_positive_source` | pointer `+&r`、u64 `+Q`、u64 `=&r`，3个 role。 | R2/U2/E1 |
| `F5-A20` `aarch64_linux_lse_dec_positive_emit` | ldr/subs/casal retry，pointer 结果与临时值都8B。 | R2/U2/E1 |
| `F5-A21` `aarch64_linux_futex_source` | 5输出+2/3输入均 u32，set/add/or/and/xor/cmpxchg，typed roles 明确。 | R2/U2/E1 |
| `F5-A22` `aarch64_linux_futex_emit` | ldxr/stlxr budget/fault labels，`__ex_table` 两处，四个32b值正确回写。 | R2/U2/E1 |
| `F5-A23` `aarch64_linux_timer_alternative_source` | CNT[P/V]CT_EL0 GNU alternative 形状、单 u64 输出。 | R2/U2/E1 |
| `F5-A24` `aarch64_linux_timer_alternative_emit` | feature19 replacement + isb/mrs，双 `.org` 长度对齐约束。 | R2/U2/E1 |
| `F5-A25` `aarch64_linux_counter_ordering_source` | eor/add sp/ldr xzr，r input 与 =r output，保留隐式 load effect。 | R2/U2/E1 |
| `F5-A26` `aarch64_linux_counter_ordering_emit` | x10 数据依赖→x9 sp 地址→ldr xzr，结果回写。 | R2/U2/E1 |
| `F5-A27` `aarch64_linux_rng_sysreg_source` | RNDR/RNDRRS，u64 result + bool status，CC clobber。 | R2/U2/E1 |
| `F5-A28` `aarch64_linux_rng_sysreg_emit` | `.inst` MRS 与 cset w10，u64 `str`、bool `strb`，窄类型保护正确。 | R2/U2/E1 · 排误报 |
| `F5-A29` `aarch64_linux_cache_op_source` | DC/IC 5种精确模板，pointer/ulong carrier，volatile 与隐式内存。 | R2/U2/E1 |
| `F5-A30` `aarch64_linux_cache_op_emit` | 当前 TargetInfo unqual type 匹配；x9 address→DC/IC 真实指令，帧形状失败拒绝。 | R2/U2/E1 |

**A64 覆盖边界：** `source_policy/aarch64.c`（120行）、`fp_emit.c`（248行）、`residual_emit.c`（793行）全读；`asm_contract.c`（7376行）只对上述生产函数/总闸与若干 descriptor 区段逐块完整核验，余下区间维持 R1。MC-052 为 Source→`.s`，缺目标 GAS/ELF/QEMU（E3）。

### 第五轮 x86-64：Linux GNU ASM 双端长尾（30 组）

Source 是 `src/target/x86_64/asm_contract.c`；Machine 是 `src/target/x86_64/machine_asm.c`。每一项的 Source 资格检查通过 `minic_x86_64_qualify_inline_asm` 到 Core operand plan，再由 `minic_x86_64_emit_structured_asm` 分发；Source 非 volatile、Core effect 强制 volatile 的转换是有意行为，不应误判。`F5-X10/27/28` 仅部分区段核对，诚实标 R1/U1；其他已核对双端真实消费者。

| ID · Source → Machine 函数简称 | 类型、ABI/副作用与写回契约 | 核验 |
| --- | --- | --- |
| `F5-X01` `minic_x86_64_qualify_inline_asm` → `minic_x86_64_emit_structured_asm` | 109 资格形状（21 typed / 88 legacy），机器对应分发，unknown fail closed。 | R2/U2/E1 |
| `F5-X02` `x86_linux_mmio_read_source` → `emit_linux_mmio_read` | 1/2/4/8B memory input + scalar result store；cv 类型两端一视同仁。 | R2/U2/E1 |
| `F5-X03` `x86_linux_mmio_write_source` → `emit_linux_mmio_write` | register scalar input+memory output，按 1/2/4/8B 宽度选物理 store。 | R2/U2/E1 |
| `F5-X04` `x86_linux_pci_mmconfig_source` → `emit_linux_pci_mmconfig` | AL/AX/EAX 固定寄存器，地址 constraint `r`，不误认为 `m`。 | R2/U2/E1 |
| `F5-X05` `x86_linux_rep_string_io_source` → `emit_linux_rep_string_io` | SI/DI+CX `+` RW、DX port，machine 保存/回写两物理寄存器。 | R2/U2/E1 |
| `F5-X06` `x86_linux_atomic_xchg_source` → `emit_linux_atomic_xchg` | readwrite register+memory，LOCK effect + 实体宽度。 | R2/U2/E1 |
| `F5-X07` `x86_linux_pvclock_scale_source` → `emit_linux_pvclock_scale` | RDX:RAX 128-bit intermediate，经 SHRD 得 high/low outputs。 | R2/U2/E1 |
| `F5-X08` `x86_xsetbv_source` → `emit_linux_xsetbv` | EAX/EDX/ECX fixed inputs；特权指令仅审发射，不执行。 | R2/U2/E1 |
| `F5-X09` `x86_fxsave_source` → `emit_linux_fxsave` | 512B/16B aligned image in memory；不能退化 scalar copy。 | R2/U2/E1 |
| `F5-X10` `x86_x87_probe_source` → `emit_linux_x87_probe` | FNSTSW/FNSTCW memory 状态；长尾条件只抽读部分区段。 | R1/U1/E1 |
| `F5-X11` `x86_linux_pkru_source` → `emit_linux_pkru` | RDPKRU EAX/EDX output、ECX input；volatile output 与 source qualifier 不一致（MC-050）。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F5-X12` `x86_linux_random_seed_source` → `emit_linux_random_seed` | RDRAND/RDSEED CF + 64-bit result；MC-050，同类 CPU 特性依赖未执行。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F5-X13` `x86_linux_unwind_context_source` → `emit_linux_unwind_context` | RIP/RSP/RBP 三输出；volatile outputs MiniC 错拒，GNU/mixed 链接原生可执行。 | R2/U2/LOCAL-COMPILE-DIFF · MC-050 |
| `F5-X14` `x86_linux_mxcsr_source` → `emit_linux_mxcsr` | STMXCSR/LDMXCSR memory，机器端已 unqualify，反例排除。 | R2/U2/E1 |
| `F5-X15` `x86_linux_fsgsbase_source` → `emit_linux_fsgsbase` | FS/GS base 64b read/write；volatile output MiniC 拒，未执行 CPU 特性依赖指令。 | R2/U2/LOCAL-COMPILE-DIFF · MC-050 |
| `F5-X16` `x86_linux_segment_read_source` → `emit_linux_segment_read` | 16-bit selector 与 32-bit widened 读；Core 强制有 effect，不是 Source 非 volatile bug。 | R2/U2/E1 · 排误报 |
| `F5-X17` `x86_linux_monitor_wait_source` → `emit_linux_monitor_wait` | MONITOR/MWAIT(X) A/C/D 或 A/B/C，RBX push/pop，未真实运行。 | R2/U2/E1 |
| `F5-X18` `x86_linux_raw_cmpxchg_source` → `emit_linux_raw_cmpxchg` | tied operand old value RAX，LOCK CMPXCHG，memory metadata。 | R2/U2/E1 |
| `F5-X19` `x86_linux_trycas_source` → `emit_linux_trycas` | ZF `=@ccz`、old `+a` memory，保留比较失败旧值。 | R2/U2/E1 |
| `F5-X20` `x86_linux_atomic_rmw_source` → `emit_linux_atomic_rmw` | 28 descriptor forms；GNU legacy `=m/m` alias 有实在消费者。 | R2/U2/E1 |
| `F5-X21` `x86_linux_atomic_cc_source` → `emit_linux_atomic_cc` | 8 RMWcc forms，ZF/SF bool，volatile status output 被错拒（MC-050）；plain GNU/native PASS。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F5-X22` `x86_linux_bitops_source` → `emit_linux_bitops` | 12 BT/BTS/BTR/BTC 等 forms，CF output；volatile bool 机器错拒（MC-050），plain/native PASS。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F5-X23` `x86_zstd_cpuid_source` → `emit_zstd_cpuid` | 3种1/3 output variant，与 native 4-output CPUID 分离，RBX 保存。 | R2/U2/E1 |
| `F5-X24` `x86_linux_kernel_nofault_goto_source` → `emit_linux_kernel_nofault_goto` | asm-goto label、8种 MOV 宽度、3-word PREL32 exception table。 | R2/U2/E1 |
| `F5-X25` `x86_linux_put_user_call_source` → `emit_linux_put_user_call` | RSP/RAX/RCX/Linux helper ABI，RBX preserved，expected offsets typed。 | R2/U2/E1 |
| `F5-X26` `x86_linux_futex_extable_source` → `emit_linux_futex_extable` | locked cmpxchg 的 `+r` 初值与两条异常表真实保留。 | R2/U2/E1 |
| `F5-X27` `x86_linux_fdiv_bug_source` → `emit_linux_fdiv_bug` | x87 state/FISTPL/reset，长尾仅部分抽读，非运行验收。 | R1/U1/E1 |
| `F5-X28` `qualified legacy fallback` → `emit_memory_clobber_matched/emit_bit_scan/emit_muldiv_u64` | CPUID/bit-scan/muldiv legacy 形式有消费者；部分代码未全读。 | R1/U1/E1 |
| `F5-X29` `minic_backend_capture_asm_operand_source` → Core lowering | Source operand provenance、tied/early-clobber/effective memory；Core `is_volatile=true` 有意。 | R2/U2/E1 |
| `F5-X30` `source_policy/x86_64.c` source qualifier policy | 固定寄存器与多种 GNU constraint admission，所有小文件 106 行已通读；未知形状拒绝。 | R2/U2/E1 |

**覆盖诚实性：** `asm_contract.c` 8,874 行、`machine_asm.c` 6,442 行，typed source switch 和 30 组双端核对已完成，但若干 EFI/VMware/MSR/XSAVE/uaccess/legacy 段仍是 R1/U1，不得报“所有函数 100%”。`x86_linux_three_call_alternative_source` 与 `x86_linux_jump_label_source` 同名两处只是 forward declaration+definition，既非死分支也非两份实现。MC-050 的六家族是 `GNU C` 合法 + 本机 GNU 严格编译 + MiniC plain PASS/cv FAIL；其中 UNWIND/ATOMIC_CC/BITOPS GNU 与 mixed 原生执行成功，不等于 CV MiniC 已经通过。

## 第六轮：独立复扫与遗漏模块补读（2026-10-30）

本轮审计起点为已发表 `74b366267`。继续执行函数阅读 R/U 与动态证据 E 分离。以下 `F6-M` 分片来自尚未有独立条目的 Target Common helper：既查看定义，也在至少一个真实 A64/Loong/x86/RV consumer 处核对；**同一行里的多个 helper 是函数组，不是独立函数数量**。未发现新的 Common wrong-code 反例，不能因“共用”本身归咎于设计错误。

### 第六轮 Target Common：数据形状、LP64 前检及机器残余计划

| ID · 函数（`src/target/common/`） | 输入/输出、所有权与真实 consumer | 核验 |
| --- | --- | --- |
| `F6-M01` `aggregate_shape.c:minic_common_aggregate_storage_shape` | R immutable Program/Target DataLayout，计算 record bytes/align 和 ceil(words)；x86/RV32/RV64/LP64 aggregate classifiers 消费，empty record 0 words 是合法结果。 | R2/U2/E1 |
| `F6-M02` `aggregate_shape.c:integral_tree/minic_common_aggregate_integral_tree` | R 递归 Record/Array 的整数与 pointer 叶子，仅做结构分类、深度限256；RV64 ABI 使用，不擅自制定物理 GPR placement。 | R2/U2/E1 |
| `F6-M03` `core_numeric_shape.c:minic_core_integer_overflow_shape` | R typed Core overflow operands/result pointee+Target widths，W caller width/sign；x86/A64/Loong 后端分别发物理 flag/carry。 | R2/U2/E1 |
| `F6-M04` `...:minic_core_integer_overflow_mixed_add_i32_shape` | R mixed i32/u64 ADD 形状，W signed-left；x86/A64/Loong 有不同机器 flag 实现。 | R2/U2/E1 |
| `F6-M05` `...:minic_core_integer_overflow_u64_u32_to_i32_shape` | R unsigned 64/32 operands + signed 32 result，W wide-left；Loong 有独立 consumer，非所有 ISA 通用 opcode。 | R2/U2/E1 |
| `F6-M06` `...:minic_core_integer_overflow_mixed_multiply_i32_shape` | R 4B mixed-sign operands/result，W caller three signedness flags；A64/Loong 消费，统一分类但不统一机器实现。 | R2/U2/E1 |
| `F6-M07` `...:minic_core_integer_overflow_narrow_result_shape` | R 1/2B result 与 up to 4B operands，W shape tuple；A64 integer overflow emitter 特化消费，保留明确边界。 | R2/U2/E1 |
| `F6-M08` `...:minic_core_integer_bitscan_shape` | R CLZ/CTZ 8/16/32/64-bit unsigned operand，W bit width；x86/RV64/A64/Loong 各自选择 count/instruction。 | R2/U2/E1 |
| `F6-M09` `core_value_slots.c:minic_core_value_slots_build` | R Core value types，OWN offsets array；linear x86 frame 与 wide-tail A64/Loong LP64 frame 是**不同 policy**，allocation 错误释放临时 offsets。 | R2/U2/E1 |
| `F6-M10` `direct_function_profile.c:minic_multi_direct_lp64_profile_function` | R Core facts + target ABI placement plan，W profile fixed GR/VR frontier；A64 mutable va_list 与 Loong linear cursor 独立，支持多个合法 va_start。 | R2/U2/E1 |
| `F6-M11` `direct_residual_plan.c:minic_multi_direct_integer_cast_plan` | R Core INTEGER_CONVERSION、source/result wide slots，W caller cast plan，A64/Loong residual emitter 共用，内存布局由 frame owner。 | R2/U2/E1 |
| `F6-M12` `...:minic_multi_direct_constant_integer_shape` | R Target width 与 int64 Core literal；W width/sign，A64/Loong emit constant，pseudo-bool 保守限制。 | R2/U2/E1 |
| `F6-M13` `...:minic_multi_direct_global_address_symbol` | R GlobalId 经 Core globals 符号借用，A64/Loong 发目标 GOT/local address；不持有/修改字符串。 | R2/U2/E1 |
| `F6-M14` `...:minic_multi_direct_function_address_symbol` | R FunctionSymbolId/type；借字符串供 A64/Loong LEA，非目标自建重定位表。 | R2/U2/E1 |
| `F6-M15` `...:minic_multi_direct_block_address_shape` | R Core label block/result pointer identity，W BlockId；两个 LP64 residual block-address emitter 分别编码。 | R2/U2/E1 |
| `F6-M16` `...:minic_multi_direct_object_address_location` | R verified Core ObjectId+frame object layout，W caller byte offset；A64/Loong 地址生成自己决定寄存器。 | R2/U2/E1 |
| `F6-M17` `...:minic_multi_direct_field_address_shape` | R Core base pointer + byte offset，W scalar metadata；A64/Loong 机器地址计算保留各自指令限制。 | R2/U2/E1 |
| `F6-M18` `...:minic_multi_direct_dynamic_stack_alloc_shape` | R Core alloca argument/result + dynamic_frame，W bool admission；两 LP64 target emit 分离实际 SP 操作。 | R2/U2/E1 |
| `F6-M19` `lp64_scalar.c:minic_lp64_scalar_integer_width` / `minic_lp64_register64_type` | R effective enum integer type、Target width/sign，明确排除 33–63 bit；ABI classifier/call/overflow/branch consumer 共用。 | R2/U2/E1 |
| `F6-M20` `lp64_scalar.c:minic_lp64_valid_parameter_type/minic_lp64_valid_return_type/minic_lp64_valid_return_with_indirect` | R C scalar/Record + target indirect-return callback；返回允许 empty/GP/HFA record 或大对象 ABI hidden pointer，A64/Loong 各有 policy。 | R2/U2/E1 |
| `F6-M21` `direct_function_plan.c:minic_multi_direct_lp64_function_plan_prepare/destroy` | OWN incoming ABI plan、frame、call sites，失败可留 partial alloc 由 destroy 收回；通过前检、profile、CFG、stack layout、CALL plan 后才允许发射。 | R2/U2/E1 |
| `F6-M22` `direct_preflight.c:direct_lp64_only_structured_asm/minic_multi_direct_lp64_function_preflight/minic_multi_direct_lp64_cfg_preflight` | R verified Core + targeted opaque qualifier；typed ASM 一一认证，记录所有现有错误类别但仍 fail closed；CFG return/branch 条件宽度最后由 ISA 发射。 | R2/U2/E1 |

**初步排误报：** `minic_core_value_slots_build` 的 `LINEAR/WIDE_TAIL` 绝非可互相删除的重复 frame 算法；`minic_multi_direct_constant_integer_shape` 对未知 C pseudo-bool 仅允许原有 int32 安全范围；Common 聚合形状的 `0 words` 并非错误本身，是否正确处理 empty record 返回属于每个目标独立 ABI/terminator 的消费责任（第六轮另测）。

### 第六轮 Frontend：初始化器、设计符与递归类型解析（28 组）

以下条目均由 Frontend 分片交叉读取本体和真实调用/扩容路径，为 `R2/U2/E1`；`ASAN` 仅表示特指测试执行过，不代表相关所有函数动态验证完成。稳定的 `RecordId/TypeId/ExpressionId` 和会重新分配的 arena 元素指针不可互换。

| ID · 函数/函数组（`src/frontend/`） | 读写、生命周期与下游契约 | 核验 |
| --- | --- | --- |
| `F6-P01` `parser_static_initializer.c:append_static_array_zero` | 以类型/尺寸追加静态零填充；调用者维护目标数组已写范围，无递归借用。 | R2/U2/E1 |
| `F6-P02` `static_integer_address_relocation_target` | 检查整数常量中的地址重定位目标，归还稳定 symbol/offset 而非暂存 Parser arena 指针。 | R2/U2/E1 |
| `F6-P03` `parse_static_integer_address_value` | GNU 常量表达式转静态地址值，消费 #P02 的符号合法性。 | R2/U2/E1 |
| `F6-P04` `parse_static_scalar_constant_at` | 根据静态初始化标量 type 选择整数/地址/FP 编码，目标数据布局负责最终宽度。 | R2/U2/E1 |
| `F6-P05` `capture_static_aggregate_array_action` | 保存数组设计符动作所需的稳定索引与常量，不保留可扩容容器项地址。 | R2/U2/E1 |
| `F6-P06` `append_static_chained_array_designator_value` | 递归解析链式设计符可能扩容 `Program.array_types`，旧 `ArrayType*` 被再次读取，**MC-049**。 | R2/U2/ASAN · MC-049 |
| `F6-P07` `parse_static_record_designator_path` | 解析字段路径并返回稳定字段/Record 身份，供后续指定初始化写入。 | R2/U2/E1 |
| `F6-P08` `append_static_record_designator_value` | 当前以值型 `MinicRecord` header 快照跨递归解析，排除 MC-023 同类借用。 | R2/U2/E1 · 排误报 |
| `F6-P09` `append_static_record_backward_zero` | 后退设计符清零使用扩容前 Record 借用，**MC-023**。 | R2/U2/ASAN · MC-023 |
| `F6-P10` `parse_static_union_constant` | Union 指定成员及 payload 初始化读取稳定快照，不跨递归保留不安全 arena 指针。 | R2/U2/E1 |
| `F6-P11` `parse_static_record_constant` | 静态 Record 顺序/指定字段按值保存 header，递归表达式写入后仍引用有效 fields。 | R2/U2/E1 |
| `F6-P12` `probe_static_array_designator_extent` / `inspect_static_array_designator` | 探测数组设计符跨度并恢复 Parser 游标，不发布临时解析结果。 | R2/U2/E1 |
| `F6-P13` `parse_static_record_array` | 按静态数组元素目标类型迭代 Record 常量，委托稳定的 Record parser。 | R2/U2/E1 |
| `F6-P14` `minic_parser_parse_preformed_static_array_definition` | 全局静态数组定义入口，输出聚合数据和重定位归 Program 所有。 | R2/U2/E1 |
| `F6-P15` `parser_runtime_initializer.c:add_array_object_element_lvalue` | 创建单个数组元素引用，借用仅在无递归的本地构造期间存在。 | R2/U2/E1 |
| `F6-P16` `add_array_object_zero_element` | 生成零填充元素的 AST 引用并写回 initializer，非解析器下一 token 所致扩容。 | R2/U2/E1 |
| `F6-P17` `parse_fixed_runtime_record_array_initializer` | 固定 Record 数组的每元素 runtime 初始化/零补，必要时依 ID 重取类型。 | R2/U2/E1 |
| `F6-P18` `add_runtime_initializer_once_read` | evaluate-once 建表达式后曾沿用可扩容 Expression 借用，**MC-035**。 | R2/U2/ASAN · MC-035 |
| `F6-P19` `parse_fixed_runtime_nested_array_initializer` | 嵌套数组递归可扩容 ArrayType arena，**MC-039**。 | R2/U2/ASAN · MC-039 |
| `F6-P20` `add_zero_initialized_record_lvalue` | 以可重取 Record 身份生成零初始化，不跨后续 parse 借旧对象。 | R2/U2/E1 |
| `F6-P21` `add_record_field_lvalue` | 字段地址 AST 构造，读取字段元数据和目标 base；字段标识按稳定 ID 消费。 | R2/U2/E1 |
| `F6-P22` `zero_runtime_record_fields_from` | 清零 Record 后续字段，本段没有可能重新分配 Record arena 的递归 parse。 | R2/U2/E1 · 排误报 |
| `F6-P23` `parse_positional_runtime_record_initializer_from` | 顺序首字段可解析匿名 Record 并扩容 `Program.records`；下一字段读旧 `record*`，**MC-056**。 | R2/U2/ASAN · MC-056 |
| `F6-P24` `parse_runtime_record_designator_target` | 将指定字段路径解析为稳定目标，未沿用 MC-056 的普通顺序游标借用。 | R2/U2/E1 |
| `F6-P25` `parse_runtime_record_designated_tail` | 处理嵌套 designated tail，字段及类型在 append 后重新定位。 | R2/U2/E1 |
| `F6-P26` `parser_postfix.c:parse_one_subscript` / `parse_one_indirect_call` | postfix 递归会扩容 AST，当前跨追加边界使用 ID/快照，未确认新悬空读取。 | R2/U2/E1 |
| `F6-P27` `parser_expression.c:expression_parse_call_arguments` / `parse_comma` | Call 参数及逗号 expression append 后只发布稳定 expression identity。 | R2/U2/E1 |
| `F6-P28` `parser_type.c:typeof` / `parser_expression.c:parse_sizeof` | 递归解析类型/匿名 Record 会增长 Program 元数据；作为 #P23/#P06 的真实上游触发器。 | R2/U2/E1 |

### 第六轮 Compiler / Inline / Reentry：可选索引与多层特化（24 组）

此表回看 Inline clone 的所有权、事实传播及 Core lower 消费者。`reentry_index` 的旧全量扫描有 standalone API 和 OOM fallback 双消费者，不是没有清理干净的“死实现”。

| ID · 函数（`src/compiler/` 或 `src/core/`） | 写集、所有权、阶段边界与消费方 | 核验 |
| --- | --- | --- |
| `F6-C01` `inline_symbolic.c:minic_inline_symbolic_static_expression_depth` | R AST/static symbol，深度限32；直接/传递 fact closure 使用；恒假 LOCAL 分支属 MC-054 DEBT。 | R2/U2/E1 |
| `F6-C02` `minic_inline_close_symbolic_expression_depth` | 递归闭包 W Program expressions，输出稳定 ExprId；跨 realloc 旧借用已登记 MC-001。 | R2/U2/E1 |
| `F6-C03` `minic_inline_symbolic_argument_expression` | R caller symbolic fact，W 新 ExprId；不能把 symbol pointer+element offset 传下去，**MC-058**。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F6-C04` `minic_inline_symbolic_variant_matches` | 按完整 symbolic ExprId/事实组合匹配 clone；区分不同 symbol+offset，不将所有 pointer 视同。 | R2/U2/E1 |
| `F6-C05` `minic_add_inline_symbolic_specialization` | W functions，新 clone 独立签名，body 依合法共享；整数/符号/布尔阶段依次消费。 | R2/U2/E1 |
| `F6-C06` `minic_specialize_inline_symbolic_calls` | W direct CALL 与 clone/expressions；first pass 函数追加安全，transitive 闭包有 MC-001 风险。 | R2/U2/E1 |
| `F6-C07` `inline_integer.c:minic_inline_integer_specialization_matches` | R integer facts，匹配现有变体；按固定阶段排序无需比较尚未写入的后续 fact。 | R2/U2/E1 |
| `F6-C08` `minic_add_inline_integer_specialization` | OWN clone signature/name，FunctionBody 共享源 id；局部失败由 Program 总析构收回。 | R2/U2/E1 |
| `F6-C09` `minic_inline_specialization_argument_value` | R caller 的 local/parameter facts 与 cast，transitive integer 参数保守求值，无 arena append。 | R2/U2/E1 |
| `F6-C10` `minic_specialize_inline_integer_calls` | W direct CALL/clone，强行 quota 2048 使合法输入整体 fail 已登记 MC-043。 | R2/U2/E1 |
| `F6-C11` `inline_boolean.c:minic_inline_boolean_range_argument` | R local bool cache 与 caller fact，W used_fact；仅认可可证明区间。 | R2/U2/E1 |
| `F6-C12` `minic_specialize_inline_boolean_range_calls_for_pass` | W direct CALL/clone；transitive 仅创建 clone 后由 Core selector 选取；quota1024 已归 MC-043。 | R2/U2/E1 |
| `F6-C13` `minic_inline_build_local_boolean_domain_cache` | OWN 四类 bitmap，所有 alloc 失败清理，成功才发布局部缓存。 | R2/U2/E1 |
| `F6-C14` `minic_inline_local_boolean_domain_legacy` | 无内存分配的全 Program 写集扫描，被缓存创建 OOM 分支实际调用；不是应删旧实现。 | R2/U2/E1 · 排误报 |
| `F6-C15` `inline_reference.c:minic_inline_integer_source_has_residual_reference` | R AST/CALL/global relocation/source refs；finalization 或 OOM fallback 消费，保守多发射正确。 | R2/U2/E1 |
| `F6-C16` `minic_finalize_inline_integer_specialization_references` | OWN 可选逐 source cache，写 clone/source referenced；失败时退回 #C15 全扫描。 | R2/U2/E1 |
| `F6-C17` `core/reentry_index.c:minic_core_reentry_index_destroy` | OWN heads/incoming/addressed，销毁可接受 NULL；CoreSet 结束前 Program 仍存活。 | R2/U2/E1 |
| `F6-C18` `minic_core_reentry_index_create` | R immutable Program CFG，OWN 索引；成功才 publish，OOM 释放并让查询 fallback。 | R2/U2/E1 |
| `F6-C19` `minic_core_reentry_index_ready` | 验 Program identity，CoreLower transaction 内 AST 不再写；query 决定 indexed/fallback。 | R2/U2/E1 |
| `F6-C20` `core_reentry_indexed_target` | R goto/asm-goto/label-address/case source roots，未知情形保守 INDETERMINATE。 | R2/U2/E1 |
| `F6-C21` `core_reentry_fallback` | 逐 AST 扫描旧算法仍被 standalone CoreLower 与 OOM 使用，不可删除。 | R2/U2/E1 · 排误报 |
| `F6-C22` `minic_core_reentry_query` | R index+Program+subtree，供应 IF/switch/loop lowering 的外部跳入语义。 | R2/U2/E1 |
| `F6-C23` `core_lower.c:minic_core_lower_function_with_index` | OWN temporary Core/objects/maps，成功 XFER output、失败销毁；reentry indexed/fallback+五 ISA `-S` 脚本已 PASS。 | R2/U2/E1·回归PASS |
| `F6-C24` `minic_core_lower_function` | standalone API 以 NULL index 委托 #C23；是 #C21 fallback 的实际生产消费者。 | R2/U2/E1 |

### 第六轮 Core IR：存储所有权、Verifier 与对象生存区间（16 组）

| ID · 函数（`src/core/` 或 `src/target/common/`） | 生产者、消费方与生命周期 | 核验 |
| --- | --- | --- |
| `F6-V01` `core_ir.c:grow_array` | count/capacity/elem_size 成功 realloc 才发布；Core enum/binding/symbol/SSA append 共用 owner。 | R2/U2/E1 |
| `F6-V02` `minic_core_function_initialize/destroy` | OWN Core 各子数组、字符串、ASM/clobber、call signature；正向 API ASan clean。 | R2/U2/ASAN |
| `F6-V03` `minic_core_function_set_signature` | OWN function name/return/parameters signature 副本；Verifier 检查传入的类型身份。 | R2/U2/ASAN |
| `F6-V04` `minic_core_function_add_enum_type` | EnumId 去重和兼容整数类型，Core metadata lowerer→后端 integer scalar 消费。 | R2/U2/ASAN |
| `F6-V05` `minic_core_function_effective_integer_type` | R enum metadata，合法未注册返回 false；人为 count>capacity 会越界，MC-055。 | R2/U2/CORE-API-ASAN |
| `F6-V06` `minic_core_function_add_fixed_register_binding` | OWN register name，后端 fixed register read/write 使用稳定 binding ID；人为 pointer=NULL 可击穿 verifier，MC-055。 | R2/U2/CORE-API-ASAN |
| `F6-V07` `minic_core_function_add_function_symbol` | OWN ELF symbol name，FUNCTION_ADDRESS 引用 ID；人为 pointer=NULL 可击穿 verifier，MC-055。 | R2/U2/CORE-API-ASAN |
| `F6-V08` `minic_core_function_add_block` | OWN block instruction ID vector，BlockId 按 CFG 稳定；entry 和 branch target 由 verifier 校验。 | R2/U2/E1 |
| `F6-V09` `minic_core_function_append_call_arguments` | OWN contiguous call-argument snapshot，OOM 回滚 count，DIRECT/INDIRECT_CALL 共用 begin/count。 | R2/U2/E1 |
| `F6-V10` `reserve_instruction/append_reserved_instruction` | 同步 reserve instruction arena 与 block ID，发布时不借用可扩容 instruction 指针。 | R2/U2/E1 |
| `F6-V11` `append_value_instruction` | 原子发布唯一 ValueId/InstructionId，Value.definition 指回；SSA verifier 检查 available/reachable。 | R2/U2/ASAN |
| `F6-V12` `append_effect_instruction/set_terminator` | effect 无 result，终结指令至多一次；分支/return 结构由 verifier 检查。 | R2/U2/E1 |
| `F6-V13` `mark_reachable_blocks` | entry 与可能外部跳入的源 label 均作为 CFG roots；后端不能仅基于 branch 删除 ASM/GOTO 可达块。 | R2/U2/E1 |
| `F6-V14` `core_address_known_alignment_visit/wrapper` | 追 Object/Global/Field/Offset 定义并 GCD 合成偏移对齐，其余来源保守返回1。 | R2/U2/E1 |
| `F6-V15` `value_uses.c:core_visit_value_use/core_visit_call_argument_uses` | 只计 VALUE SSA uses；OBJECT 实参交给 object interval 的独立追踪，不是漏用。 | R2/U2/E1 |
| `F6-V16` `core_object_intervals.c:core_object_address_build_use_index/core_object_interval_build_index` | LOAD/STORE 识别 direct use，ATOM/RECORD/ASM 仍计 total；total!=direct 禁止复用。 | R2/U2/E1 |

**Verifier 资格边界：** 本轮逐个核对 **69/69 个有效 Core instruction opcode** 在 verifier 和 value-use visitor 中的分支，不等于所有形状/取值都合法。MC-055 的三个损坏数组为手动构造的 malformed Core；未找到正常 GNU C lowering 导致相同损坏的证据，见 Bug 台账。

### 第六轮 RISC-V：RV32 参数/栈与 RV64 helper/return（30 组）

| ID · 函数（`src/target/`） | ABI/物理存储、生产者与消费方 | 核验 |
| --- | --- | --- |
| `F6-R01` `riscv32/rv32_frame.c:rv32_frame_init_with_storage` | R Core+ILP32 ABI plan，OWN frame objects/SSA/varargs/outgoing space；rv32_func 生产消费者。 | R2/U2/E1 |
| `F6-R02` `rv32_frame_destroy` | OWN frame 内存释放，在 rv32_func 的成功/失败出口调用。 | R2/U2/E1 |
| `F6-R03` `rv32_stack_at` | R frame+偏移；超过 immediate 用 t6 materialize 且避免覆盖 base。 | R2/U2/E1 |
| `F6-R04` `rv32_stack` | outgoing CALL stack 写入使用 SP 基址，与 incoming entry SP 身份区分。 | R2/U2/E1 |
| `F6-R05` `rv32_fixed_stack` | callee local/incoming frame slot 的 s0 固定栈访问。 | R2/U2/E1 |
| `F6-R06` `rv32_slot` | SSA value spill 的宽度/边界及实际 frame offset 计算。 | R2/U2/E1 |
| `F6-R07` `rv32_pair_slot` | ILP32 long long/FP low/high 两 word 的物理栈读写。 | R2/U2/E1 |
| `F6-R08` `rv32_address` | Record/object/parameter 地址产生，base alias 安全。 | R2/U2/E1 |
| `F6-R09` `rv32_incoming_word` | 形参/Record 读取，使用 entry SP 恢复而不是 outgoing SP。 | R2/U2/E1 |
| `F6-R10` `rv32_outgoing_word` | call 实参 word 的 a寄存器/栈分配，含 split64。 | R2/U2/E1 |
| `F6-R11` `riscv32/rv32_call.c:rv32_emit_object_word` | 按 bytes 拷贝 aggregate partial word，避免越 object bounds。 | R2/U2/E1 |
| `F6-R12` `rv32_emit_scalar_call_arguments` | 先排 outgoing 栈后设寄存器，避免求值覆盖参数值。 | R2/U2/E1 |
| `F6-R13` `rv32_emit_call` | 读 ABI call plan，直接/间接 CALL 与 sret 的物理传递。 | R2/U2/E1 |
| `F6-R14` `rv32_emit_parameter_object` | PARAMETER_OBJECT 的 incoming record，empty param 无机器写入。 | R2/U2/E1 |
| `F6-R15` `rv32_emit_record_return` | 已处理非零大小 aggregate；zero-byte record return 未由 caller 接管，MC-057。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F6-R16` `rv32_emit_parameter` | 标量 named 参数及跨 a7/stack 的 64-bit 组合。 | R2/U2/E1 |
| `F6-R17` `riscv32/rv32_emit.c:rv32_term` | CFG terminator/return dispatch，empty record 落 scalar-only reject，MC-057。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F6-R18` `rv32_func` | TU 核心函数消费者；frame/prologue/variadic spill/CFG/epilogue 和 destroy。 | R2/U2/E1 |
| `F6-R19` `riscv32/rv32_abi_plan.c:rv32_plan_shape` | empty record 正确产生 zero words，并未创造伪 ABI 返回值。 | R2/U2/E1 |
| `F6-R20` `rv32_plan_incoming` | caller/callee 统一的 return/param/sret placement。 | R2/U2/E1 |
| `F6-R21` `rv32_plan_call` | formal/actual、vararg 64bit 偶数寄存器对齐、可能的栈切换。 | R2/U2/E1 |
| `F6-R22` `rv32_function_abi_plan_build` | 一次构造稀疏 CALL index 与函数入口 plan，frame_init 消费。 | R2/U2/E1 |
| `F6-R23` `rv32_function_abi_call` | 以稳定 CallSite ID 读 plan，rv32_emit_call 消费。 | R2/U2/E1 |
| `F6-R24` `riscv64/core_codegen.c:emit_terminator` | Core RETURN 对 `ABI_VALUE_IGNORE` 没有分支，empty record rejected，MC-057。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F6-R25` `riscv64/frame_layout.c:minic_riscv64_core_frame_initialize_with_storage` | OWN prepared ABI/frame cache；helper CALL 必须保留 RA。 | R2/U2/E1 |
| `F6-R26` `riscv64/frame_requirements.c:minic_riscv64_call_plan_cache_build` | 单 CallSite ABI plan 被 Core admission/frame/emitter 使用。 | R2/U2/E1 |
| `F6-R27` `common/core_object_reuse.c:minic_core_object_reuse_plan_build` | RV32/RV64 共用 8B object reuse pool，align/size资格另查。 | R2/U2/E1 |
| `F6-R28` `common/core_object_intervals.c:minic_core_object_interval` | size>8/overaligned object 不入 8B 复用池，排除 overalign 误报。 | R2/U2/E1 |
| `F6-R29` `common/core_spill_layout.c:core_value_spill_slot_size` | 依据 target XLEN 为 SSA temporary 分配1/2 word，不代替对象对齐布局。 | R2/U2/E1 |
| `F6-R30` `common/core_spill_layout.c:minic_core_function_reused_spill_layout` | block-local liveness 和 SSA spill slot reuse，RV32/RV64 frame consumer。 | R2/U2/E1 |

**RISC-V 排误报：** RV32 单个可用参数寄存器时 64bit 参数被拆为 a7 低字+栈高字，普通 va_arg 64bit 落偶数 a2/a3；RV64I/IM/LP64D 下需 libgcc 的 helper CALL 均配套保存 RA。以上仅是编译/物理汇编检查，不是 RV QEMU 执行验收。

### 第六轮 AArch64：GNU ASM 的 Source→Core→Machine 双端资格（55 组）

以下每条定位 `src/target/aarch64/asm_contract.c`，并回查 `source_policy/aarch64.c` 的 Source 约束、`frame_emit.c` 的物理寄存器/stack-slot consumer；`R2/U2/E1` 意味着阅读和静态契约已核对，**没有 55 次目标 ELF/QEMU 运行证明**。第五轮已登记 MC-052 本轮未重复编号。

| ID · Source / Emit 函数或函数组 | 关键输入、ABI 写回、物理消费者 | 核验 |
| --- | --- | --- |
| `F6-A01` `aarch64_owned_reg_policy` | 按 source operand 的寄存器拥有权决定临时 x-reg 可用范围。 | R2/U2/E1 |
| `F6-A02` `aarch64_atomic_owned_x` | 原子模板中 x 寄存器所有权/冲突判定，机器 CAS/LLSC 消费。 | R2/U2/E1 |
| `F6-A03` `aarch64_atomic_owned_w` | 32bit w 寄存器约束独立于64bit x 格式。 | R2/U2/E1 |
| `F6-A04` `aarch64_atomic_owned_x17_ip0_scratch` | x17/IP0 在当前原子模板作为有限机器 scratch 的活跃性。 | R2/U2/E1 |
| `F6-A05` `aarch64_futex_owned_x17_x12_scratch` | futex fault 分支保留 x17/x12 的源/机器所有权边界。 | R2/U2/E1 |
| `F6-A06` `aarch64_counter_owned_x` | counter ordering 中地址依赖寄存器不能被通用 spill 覆盖。 | R2/U2/E1 |
| `F6-A07` `aarch64_privileged_owned_reg/_x/_w` | Linux sysreg/privileged ASM 的固定寄存器与宽度准入。 | R2/U2/E1 |
| `F6-A08` `aarch64_linux_jump_label_form` | 精确匹配 Linux jump_label GNU asm-goto 形态。 | R2/U2/E1 |
| `F6-A09` `aarch64_linux_jump_key_relocation` | 保存 patch-site 与 key relocation 的符号/section 关系。 | R2/U2/E1 |
| `F6-A10` `minic_aarch64_qualify_inline_asm_goto` | Source GNU asm-goto→Core label/operand contract，拒未支持类型。 | R2/U2/E1 |
| `F6-A11` `minic_aarch64_linux_jump_label_asm_goto_core` | 结构化 Core 形状和 patch table 的两个 source 身份。 | R2/U2/E1 |
| `F6-A12` `minic_aarch64_emit_linux_jump_label_asm_goto` | 机器 patch-site/relocation/alternative table，不能当无 effect 普通跳转。 | R2/U2/E1 |
| `F6-A13` `aarch64_linux_cap_goto_text` | GNU capability alternative 的目标 label/text 资格。 | R2/U2/E1 |
| `F6-A14` `minic_aarch64_linux_cap_asm_goto_source` | capability goto 源 operand/label 绑定。 | R2/U2/E1 |
| `F6-A15` `minic_aarch64_linux_cap_asm_goto_core` | Core 多路径 label/alternative 元数据形状。 | R2/U2/E1 |
| `F6-A16` `minic_aarch64_emit_linux_cap_asm_goto` | 后端 cap goto relocation/section 发射；延期 symbol link 不自动等于新 bug。 | R2/U2/E1 |
| `F6-A17` `aarch64_linux_structured_spill_slot_valid` | 物理 spill slot 地址/宽度资格，所有 ASM output writeback 消费。 | R2/U2/E1 |
| `F6-A18` `aarch64_linux_pstate_imm_opcode` | PSTATE immed 系列精确数字 opcode 资格。 | R2/U2/E1 |
| `F6-A19` `minic_aarch64_linux_daif_immediate_form` | DAIF 中断掩码写入的立即数与 opcode 精确匹配。 | R2/U2/E1 |
| `F6-A20` `aarch64_linux_rept_nop_count` | GNU `.rept/.endr` nop 数量保留 patch size。 | R2/U2/E1 |
| `F6-A21` `aarch64_linux_qualified_opaque_asm` | 已认证零操作数特权 ASM 仍保持显式 side effect。 | R2/U2/E1 |
| `F6-A22` `aarch64_linux_sysreg_u64_type` | 源端 u64/ulong/volatile cv 形状，machine 使用 x 寄存器。 | R2/U2/E1 |
| `F6-A23` `aarch64_linux_mrs_register` | MRS 读 system register→64b carrier。 | R2/U2/E1 |
| `F6-A24` `aarch64_barrier_template` | Barrier/dsb/isb 指令的 GNU 完整模板。 | R2/U2/E1 |
| `F6-A25` `aarch64_linux_brk_template/_value` | BRK 即时数合法范围与机器 `.inst` 输出。 | R2/U2/E1 |
| `F6-A26` `aarch64_raw_io_read` | MMIO READ 的源输出类别→物理 byte/half/word/x 宽度。 | R2/U2/E1 |
| `F6-A27` `aarch64_raw_io_store` | MMIO WRITE 的寄存器数据/地址和内存 side effect。 | R2/U2/E1 |
| `F6-A28` `aarch64_load_acquire_width` | acquire load 按 1/2/4/8B 选择真实指令。 | R2/U2/E1 |
| `F6-A29` `aarch64_stlr_width` | STLR store-release 对象宽度判定。 | R2/U2/E1 |
| `F6-A30` `aarch64_stlr_scalar_source/_core` | Source output/operand → Core memory effect → scalar STLR。 | R2/U2/E1 |
| `F6-A31` `aarch64_stlr_memory_carrier` | STLR 指针 carrier / local lvalue，不把内存误作 GPR 类型。 | R2/U2/E1 |
| `F6-A32` `aarch64_lse_form` | Linux LSE atomics 精确操作码/内存序/宽度 matcher。 | R2/U2/E1 |
| `F6-A33` `minic_aarch64_requires_qualified_input_asm` | ASM 输入必须由 source adapter 认证的门控。 | R2/U2/E1 |
| `F6-A34` `aarch64_linux_alt_partition` | 解析 altinstruction old/new 分块，保留 section/layout。 | R2/U2/E1 |
| `F6-A35` `aarch64_linux_alt_emit_middle` | 输出替代机器指令及双 `.org` 对齐元数据。 | R2/U2/E1 |
| `F6-A36` `aarch64_linux_alt_emit_end` | alternative section/length 封尾，消费者依赖相同 patch 尺寸。 | R2/U2/E1 |
| `F6-A37` `aarch64_linux_pstate_alternative` | PAN/MTE 无 operand feature alternative；source 与机器 emit 的双 consumer。 | R2/U2/E1 |
| `F6-A38` `aarch64_linux_kvm_va_mask_form` | EL2 masked pointer 及四段 alt 元数据；输入必须 pointer/u64。 | R2/U2/E1 |
| `F6-A39` `aarch64_atomic_form` | 32/64b RMW descriptor 到 LL/SC/LSE 机器模板。 | R2/U2/E1 |
| `F6-A40` `aarch64_linux_percpu_rmw_form` | 8/16/32/64 per-cpu atomic，窄结果物理 `strb/strh`。 | R2/U2/E1 |
| `F6-A41` `aarch64_atomic_return_form` | returning-add 的 LLSC 匹配，32/64b 结果写回。 | R2/U2/E1 |
| `F6-A42` `aarch64_linux_xchg_form` | xchg 宽度及 acquire/release，machine 使用 LL/SC。 | R2/U2/E1 |
| `F6-A43` `aarch64_linux_cmpxchg_form` | CAS/LLSC cmpxchg 内存序与 old-value 输出形状。 | R2/U2/E1 |
| `F6-A44` `aarch64_linux_percpu_alternative_template` | CPU-offset alternative 保留 TPIDR_EL1/EL2 sysreg 来源。 | R2/U2/E1 |
| `F6-A45` `aarch64_linux_percpu_write_alternative_template` | Per-CPU 写 sysreg feature 替代，目标机器 separate template。 | R2/U2/E1 |
| `F6-A46` `aarch64_linux_barrier_alternative_opaque` / `minic_aarch64_emit_linux_barrier_alternative_opaque` | Opaque 零 operand 经专门 Core effect→machine section 元数据。 | R2/U2/E1 |
| `F6-A47` `aarch64_linux_kvm_at_fixup_template` | KVM `AT` fixup 两 PC/异常表，机器写 32b error+2×64b output。 | R2/U2/E1 |
| `F6-A48` `aarch64_linux_at_s1e1r_template` | 单操作数 `AT`，`volatile unsigned long` 输入实际 -S PASS。 | R2/U2/E1 · 排误报 |
| `F6-A49` `aarch64_linux_io_dependency_form` | address dependency `add ... sp`，u64 volatile input/output -S PASS。 | R2/U2/E1 · 排误报 |
| `F6-A50` `aarch64_linux_encoded_sysreg_form` | MRS raw opcode 精确 tuple 和单 u64 output。 | R2/U2/E1 |
| `F6-A51` `aarch64_linux_encoded_sysreg_write_form` | MSR tuple 及 table，64b input/no output。 | R2/U2/E1 |
| `F6-A52` `aarch64_linux_vhe_sysreg_alternative_form` | TPIDR/EL2/VHE 二 feature alternatives 的指令形状。 | R2/U2/E1 |
| `F6-A53` `aarch64_linux_tlbi_form` | TLBI 指令名/可选地址，Core implicit memory effect 阻止删除。 | R2/U2/E1 |
| `F6-A54` `aarch64_linux_hyp_spin_form` | KVM ticket lock 的 output/pack/宽度，两类 source 形状分别适配。 | R2/U2/E1 |
| `F6-A55` `aarch64_linux_cmpwait_form` | exclusive wait 8/16/32/64b，8B tmp 和 value 通过 `str x9` 对应。 | R2/U2/E1 |

**边界重申：** `asm_contract.c` 第五轮与第六轮累计已读绝大多数生产契约区段，但 typed/source/machine 的合法匹配不证明 GNU AS/QEMU。`source_policy` 的 `Q/I/x/r` 只做 source 资格，最终 frame emitter 还需遵守 SP/x16/x17 scratch 和物理宽度，已登记 MC-052 仍待 8B writeback 修正。

### 第六轮 x86-64：EFI/KVM/XSAVE/uaccess 长尾与双路径排误报（29 组）

对照 `src/target/x86_64/asm_contract.c` 的 Source 与 `machine_asm.c` 的 emit。结构化路径是 qualify→Core STRUCTURED_INLINE_ASM→machine emitter；零 operand 的精确模板经 Core OPAQUE→`machine_emit.c`，两条路径可能复用同一 emit 函数，**不要只根据一个 qualifier 的 `return false` 就宣布机器 case 死代码**。五个大型分支 R1/U1 继续保留缺口。

| ID · Source → Machine 函数简写 | 源输出、物理输入及布局契约 | 核验 |
| --- | --- | --- |
| `F6-X01` `x86_linux_pm_trace_source` → `emit_linux_pm_trace_record` | `.tracedata`/line number/section relocation，长尾部分核对。 | R1/U1/E1 |
| `F6-X02` `x86_linux_crash_register_source` → `emit_linux_crash_register_snapshot` | GPR/RFLAGS/RIP 快照与多输出宽度，部分核对。 | R1/U1/E1 |
| `F6-X03` `x86_linux_vmware_stealclock_alt_source` → `emit_linux_vmware_stealclock_alt` | EAX/ECX+RBX save，CPU alternative。 | R2/U2/E1 |
| `F6-X04` `x86_linux_kvm_hypercall_alt_source` → `emit_linux_kvm_hypercall` | fixed-register ABI/RBX save/alternative metadata。 | R2/U2/E1 |
| `F6-X05` `x86_linux_exception_stack_call_source` → `emit_linux_exception_stack_call` | R11/RSP 栈切换及 return-address 保留。 | R2/U2/E1 |
| `F6-X06` `x86_linux_alt_reloc_selftest_source` → `emit_linux_alt_reloc_selftest` | alternative 中 C global relocation，部分核对。 | R1/U1/E1 |
| `F6-X07` `x86_linux_real_mode_far_jump_source` → `emit_linux_real_mode_far_jump` | m16:32 far jump 非本机安全执行，部分核对。 | R1/U1/E1 |
| `F6-X08` `x86_linux_vmware_hypercall_source` → `emit_linux_vmware_hypercall` | 4 input/4 output 与 RBX save。 | R2/U2/E1 |
| `F6-X09` `x86_linux_legacy_fnsave_source` → `emit_linux_legacy_fnsave` | x87 状态 108B 以上 memory image。 | R2/U2/E1 |
| `F6-X10` `x86_linux_kexec_load_segments_source` → `emit_linux_kexec_load_segments` | 五个 segment registers 的 source/machine 对应。 | R2/U2/E1 |
| `F6-X11` `x86_linux_efi_kernel_transfer_source` → `emit_linux_efi_kernel_transfer` | RSI boot params/R11 indirect jump，未真正进入 EFI 执行。 | R2/U2/E1 |
| `F6-X12` `x86_linux_msr_source` → `emit_linux_msr` | RDMSR/WRMSR/异常表多特权分支，部分核对。 | R1/U1/E1 |
| `F6-X13` `x86_linux_kernel_nofault_goto_source` → `emit_linux_kernel_nofault_goto` | 已于 F5-X24 审过，本轮重复核对八种宽度、asm-goto 与 exception table。 | R2/U2/E1 · 复核 |
| `F6-X14` `x86_linux_fpu_user_frame_source` → `emit_linux_fpu_user_frame` | SMAP patch/x87/FXSAVE/FXRSTOR 与 type15 metadata。 | R2/U2/E1 |
| `F6-X15` `x86_linux_task_size_alt_source` → `emit_linux_task_size_alt` | CPU TASK_SIZE alternative 的三个固定整数。 | R2/U2/E1 |
| `F6-X16` `x86_linux_x87_fild_prepare_source` → `emit_linux_x87_fild_prepare` | FNCLEX/EMMS/FILD 的状态依赖。 | R2/U2/E1 |
| `F6-X17` `x86_linux_fpu_kernel_restore_source` → `emit_linux_fpu_kernel_restore` | 同地址 `=m/m` alias、type6 exception。 | R2/U2/E1 |
| `F6-X18` `x86_linux_fxrstor_safe_source` → `emit_linux_fxrstor_safe` | EAX error、state image 和 type17 exception。 | R2/U2/E1 |
| `F6-X19` `x86_linux_xstate_fault_source` → `emit_linux_xstate_fault` | RDI ptr、EAX/EDX mask、type15。 | R2/U2/E1 |
| `F6-X20` `x86_linux_xrstor_alternative_source` → `emit_linux_xrstor_alternative` | feature+type6 exception；无 privileged/runtime 验收。 | R2/U2/E1 |
| `F6-X21` `x86_linux_xsave_triple_alternative_source` → `emit_linux_xsave_triple_alternative` | 3 alternative patches 与 type17 exception。 | R2/U2/E1 |
| `F6-X22` `x86_linux_lar_selector_source` → `emit_linux_lar_selector` | u16 input/u32 output/ZF；GNU strict/native 与 MiniC mixed native 均 exit0。 | R2/U2/GNU-NATIVE-POSITIVE |
| `F6-X23` `x86_linux_rep_movsb_alternative_source` → `emit_linux_rep_movsb_alternative` | RCX/RDI/RSI readwrite、RSP/FSRM CALL。 | R2/U2/E1 |
| `F6-X24` `x86_linux_get_user_call_source` → `emit_linux_get_user_call` | RDX 数据/EAX error/真实 SP 和 1/2/4/8 字节。 | R2/U2/E1 |
| `F6-X25` `x86_linux_pat_clflushopt_source` → `emit_linux_pat_clflushopt` | `+m` byte address 与 alternative feature。 | R2/U2/E1 |
| `F6-X26` `x86_linux_idle_clflush_alt_source` → `emit_linux_idle_clflush_alt` | NOP baseline/CLFLUSH replacement/RAX。 | R2/U2/E1 |
| `F6-X27` `x86_linux_verw_clear_source` → `emit_linux_verw_clear` | 16b selector、cc 隐式影响。 | R2/U2/E1 |
| `F6-X28` `x86_linux_debug_register_source` → `emit_linux_debug_register` | DR0/1/2/3/6/7，DR7 fake-memory 只作为依赖不能读取。 | R2/U2/E1 |
| `F6-X29` `minic_x86_64_linux_smap_alternative_form` → `emit_linux_smap_alternative` | form1/2 零 operand 经 OPAQUE；form3/4 有 operand 经 STRUCTURED；GNU/MiniC -S/GNU AS/readelf 均有 alt sections。 | R2/U2/GNU-AS-POSITIVE · 排误报 |

**x86 排误报：** SMAP form1/2 虽被 structured qualifier 过滤，其生产者是 `core_lower_asm.c` 的 opaque branch，机器发射仍存活。LAR selector 已完成 GNU 与 mixed native 正控。两份较大文件其余未逐段深读区和内核特权指令仍不能宣布 100% 运行正确性；已有 MC-050 volatile 输出匹配缺口仍独立开放。

### 第六轮函数审计总账与证据口径

- 新增审计记录为 **204 个函数/函数组入口**：Target Common 22、Frontend 28、Compiler/Inline/Reentry 24、Core 16、RISC-V 30、AArch64 55、x86-64 29。这里存在 helper 分组，也存在重新复审第五轮同一函数（例如 F6-X13）；**不是 204 个独立新增函数**。
- 已确认新问题 **MC-056/057/058**；MC-055 提升到 Core API ASan 证据，但尚未证明正常 C producer 出错；其他候选（SMAP 双消费者、RV32 64-bit split、A64 cv、optional reentry 全扫描）被反证。
- 函数 `R2/U2` 的分母只在本表对应条目内成立；若有 `R1/U1`，不按完整已读计数。第五轮 208 记录、第六轮 204 记录不合并成“412 个独立已读函数”；没有全局 100% 结论。
- 动态证据涵盖一条五目标 ASan UAF、部分 GNU native/GNU AS 正控和若干 `-S` 差分；**本轮没有执行跨架构 Linux image/QEMU 完整验收**。

## 第七轮：每函数 R2/U2 及跨层消费者复核（2026-10-30）

基线为已发布 `820116e59`。本轮**记录 212 条函数/函数组复审条目**（Frontend31、Compiler/Core事实30、Core/Reachability33、RISC-V36、AArch64/Common49、x86长尾5、LoongArch64 28）；这些记录有重复阅读和共用 helper，不是 212 个首次发现的独立函数，更不代表整个 MiniC 的 100% 函数覆盖。`R2/U2` 表示函数体/不变式及真实 caller/consumer 两端已读，`E1` 表示仅静态路径；有独立复现才标 `ASAN`、`GNU-NATIVE-DIFF` 或 `LOCAL-COMPILE-DIFF`。编号 `F7-*` 独立于缺陷 `MC-*`。

### F7-C：Compiler/Core 的事实生命周期、函数特化与规范化循环（30 项）

| ID · 源文件与函数 | 生产者／所有权／消费者契约 | 覆盖 |
| --- | --- | --- |
| `F7-C01` `core/local_facts.c:minic_core_local_fact_index` | 本地稳定 LocalId → facts 槽索引；Core lowering/specialization 取相同 local 身份，未写 Program。 | R2/U2/E1 |
| `F7-C02` `local_facts.c:minic_core_local_facts_snapshot` | OWN 当前值/非空 pointer facts 的快照供 CFG fork；失败调用方保留原 facts。 | R2/U2/E1 |
| `F7-C03` `local_facts.c:minic_core_local_facts_restore` | 将所持 snapshot 归还工作 facts，恢复已知条件；IF/switch 分支入口消费。 | R2/U2/E1 |
| `F7-C04` `local_facts.c:minic_core_local_facts_meet` | 多分支合流仅保留一致的 known facts；IF/loop Core Lower 消费，不能“取最后分支”。 | R2/U2/E1 |
| `F7-C05` `local_facts.c:minic_core_local_facts_clear_known` | 清理 value/symbolic/null 已知状态，保留 local 身份；未知副作用与重入入口消费。 | R2/U2/E1 |
| `F7-C06` `local_facts.c:minic_core_local_fact_invalidate` | local 写后按标识失效化一个事实；assignment/increment 等 Core 用户调用。 | R2/U2/E1 |
| `F7-C07` `local_facts.c:minic_core_local_fact_escape` | 地址逃逸时保守失效，Core call/地址 consumer 不借旧事实。 | R2/U2/E1 |
| `F7-C08` `local_facts.c:minic_core_local_fact_get` | 获取当前 known scalar 值；CFG constant evaluator 仅在有效标记时复用。 | R2/U2/E1 |
| `F7-C09` `local_facts.c:minic_core_local_null_pointer_set` | 根据明确 null 证据写 pointer fact；expression/branch lower 查询。 | R2/U2/E1 |
| `F7-C10` `local_facts.c:minic_core_local_null_pointer_get` | 读取已知 null/non-null，不将缺失标记推断为 false；CFG consumer。 | R2/U2/E1 |
| `F7-C11` `core/cfg_constant_calls.c:core_cfg_eval_callee_return_expression` | 仅在被调用体与局部事实可静态求值时返回常量，超预算/side-effect 保守失败。 | R2/U2/E1 |
| `F7-C12` `cfg_constant_calls.c:minic_core_cfg_pure_call_argument` | 证明实参无效果后才考虑 call elision；volatile load 实测保留，Core value consumer。 | R2/U2/GNU-ASM-CTRL |
| `F7-C13` `cfg_constant_calls.c:core_cfg_small_loop_target_local` | 匹配可解释的单 local 微循环，失配→false，常量调用折叠消费。 | R2/U2/E1 |
| `F7-C14` `cfg_constant_calls.c:core_cfg_small_loop_add_values` | 有界整数求和遵循类型宽度/有符号性，溢出/未知停止静态解释。 | R2/U2/E1 |
| `F7-C15` `cfg_constant_calls.c:core_cfg_small_loop_update_expression` | 提取 micro-loop 的 local 更新表达式，只产生临时解释值。 | R2/U2/E1 |
| `F7-C16` `cfg_constant_calls.c:core_cfg_small_loop_execute_statement` | 解释有限 stmt 集合，遇未知副作用/形状立即失败，非运行时循环替代。 | R2/U2/E1 |
| `F7-C17` `cfg_constant_calls.c:core_cfg_small_constant_loop_call` | 临时 callee_ctx+独立 facts 最多64轮/≤32 locals，失败释放并返回 false；CFG constant inline consumer。 | R2/U2/E1 |
| `F7-C18` `cfg_constant_calls.c:minic_core_cfg_constant_inline_call` | 常量 IF/单返回/微循环求值成功才 inline；深度或形状不合→ordinary Core call。 | R2/U2/GNU-NATIVE-CTRL |
| `F7-C19` `core/source_loop_shapes.c:minic_core_loop_normalized_do_while_zero_body` | 识别 parser synthetic while/continue-label，借原 source Block view；loop lower 不能改变 Program。 | R2/U2/E1 |
| `F7-C20` `source_loop_shapes.c:minic_core_loop_normalized_do_while_block_needs_exit` | 递归识别 break/continue/goto 是否要求尾部 exit；未知时保守保留。 | R2/U2/E1 |
| `F7-C21` `source_loop_shapes.c:minic_core_loop_normalized_do_while_true_body` | 只在条件常真、synthetic tail 已证时裁掉不可达尾段；Core loop lower 消费。 | R2/U2/E1 |
| `F7-C22` `source_loop_shapes.c:minic_core_loop_normalized_for_continue_tail` | 由 synthetic label 建立 iteration view；来源位置不同则不重写。 | R2/U2/E1 |
| `F7-C23` `source_loop_shapes.c:minic_core_loop_normalized_for_update_tail` | 精确识别 for update 的 synthetic tail 与裁剪长度；失败回普通 lowering。 | R2/U2/E1 |
| `F7-C24` `core/core_lower_calls.c:core_select_transitive_integer_specialization` | 从已构造 clone 选整数事实最具体兼容者，只有 candidate ID 改变，AST 不修改。 | R2/U2/E1 |
| `F7-C25` `core_lower_calls.c:core_select_transitive_symbolic_specialization` | 在整数基底已定后细化 symbol fact；未匹配返回原 callee，不污染共享 CALL。 | R2/U2/E1 |
| `F7-C26` `core_lower_calls.c:core_select_boolean_range_specialization` | 需整数/符号事实与 base 一致，再匹配 `_Bool` 范围；结果供 call lower。 | R2/U2/E1 |
| `F7-C27` `core_lower_calls.c:lower_record_call_argument_object` | Record VALUE 复制为 owned Core object snapshot；失败→事务错误，不复用变化的临时 AST pointer。 | R2/U2/E1 |
| `F7-C28` `core_lower_calls.c:core_prepare_call_argument` | scalar/record 实参求值并 spill snapshot，在 call point 前固定副作用顺序；三类 call consumer。 | R2/U2/E1 |
| `F7-C29` `core_lower_calls.c:core_reload_call_arguments` | 真正调用块重载前置 scalar snapshot，Record 保留 OBJECT ID；direct/indirect call consumer。 | R2/U2/E1 |
| `F7-C30` `core_lower_calls.c:minic_core_lower_direct_call` | 按三个 selector→arg snapshots→签名→Core CALL；只能在证明无效应时消去 call。 | R2/U2/GNU-NATIVE-CTRL |

**F7-C 复核边界：** `compiler.c` 阶段为 parse/AST verify → normalize+verify → inline refs/clone/finalize → Core lower+prune+verify → DataLayout → target emit；Core FunctionSet 在 Program 之前释放。索引缓存只服务同一 Program 的只读 stmt/expr，构建失败可 legacy fallback。`MINIC_BOOTSTRAP_TRACE` 为诊断开关，不是 profile-guided optimizer。常量 inline/特化混合调用和 volatile 实参已有 GNU x86 native/ASM 正控；它们不构成所有 CFG 的正确性保证。

### F7-V：Core IR 身份与函数可达性（33 项）

| ID · 源文件与函数 | 生产者／所有权／消费者契约 | 覆盖 |
| --- | --- | --- |
| `F7-V01` `compiler/core_function_set.c:minic_prepare_core_function_set` | 对源 FunctionId 创建 Core slot，逐函数 lower 后供 prune/validate，失败的临时对象有 owner。 | R2/U2/E1 |
| `F7-V02` `compiler/compiler.c:minic_validate_core_functions` | 只对 prune 后 is_referenced 的 Core function 诊断；依赖 lowering 内 verifier。 | R2/U2/E1 |
| `F7-V03` `compiler.c:minic_compile_preprocessed_file_for_target` | 按规范化→Core→prune→验证→layout→emit 生命周期调度；MC-064 在 prune/emit 交界。 | R2/U2/E1 |
| `F7-V04` `compiler/core_reachability.c:minic_core_named_function_id` | assembler/name 查源 FunctionId，区分名字与稳定身份。 | R2/U2/E1 |
| `F7-V05` `core_reachability.c:minic_enqueue_core_named_function` | 仅对有定义、未到达函数入队，consumer 追踪 CALL/ADDRESS。 | R2/U2/E1 |
| `F7-V06` `core_reachability.c:minic_enqueue_core_block` | CFG block queue 去重并校验范围；reachability walker consumer。 | R2/U2/E1 |
| `F7-V07` `core_reachability.c:minic_walk_reachable_core_edges` | Core CALL/FUNCTION_ADDRESS 根可追踪；OPAQUE ASM 中符号引用漏边 **MC-064**。 | R2/U2/ASM-ONLY |
| `F7-V08` `core_reachability.c:minic_inline_source_has_noncore_root` | 入口/force_emit/alias/全局 relocation source root 慢路径；不存在 ASM 自动根。 | R2/U2/E1 |
| `F7-V09` `core_reachability.c:minic_build_noncore_root_index` | 构建相同根的加速 index，失败退慢路径，缓存不是语义 owner。 | R2/U2/E1 |
| `F7-V10` `core_reachability.c:minic_inline_source_is_core_reachable_only` | 普通静态函数不能仅凭 parser `is_referenced` 保留；MC-064 来源文本边漏传。 | R2/U2/E1 |
| `F7-V11` `core_reachability.c:minic_prune_inline_specializations_from_core` | 重算 clone/普通 static 的发射可达性；仅 source/FunctionId 保存 Core IR。 | R2/U2/ASM-ONLY |
| `F7-V12` `core/core_ir.c:minic_core_function_mark_reachable_blocks` | entry 与 source label 是保守根，RV 发射与 walker 共用 policy。 | R2/U2/E1 |
| `F7-V13` `core_ir.c:minic_core_function_initialize` | CoreFunction 零初始化及 INVALID entry，真正内存由后续 add 拥有。 | R2/U2/E1 |
| `F7-V14` `core_ir.c:minic_core_function_destroy` | 递归释放 owned block/callee/ASM/fixed metadata，正常构造正控通过；malformed API 属 MC-055。 | R2/U2/E1 |
| `F7-V15` `core_ir.c:minic_core_function_add_enum_type` | 枚举 effective-type 元数据注册及去重，Core scalar ABI consumer。 | R2/U2/E1 |
| `F7-V16` `core_ir.c:minic_core_function_effective_integer_type` | enum compatibility→物理 integer representation，RV32/RV64 emitter 消费。 | R2/U2/E1 |
| `F7-V17` `core_ir.c:minic_core_function_add_function_symbol` | typed FUNCTION_ADDRESS symbol pool，reachability 有明确身份边。 | R2/U2/E1 |
| `F7-V18` `core_ir.c:minic_core_function_add_opaque_inline_asm` | OWN 展开后纯文本+clobber，缺 relocation function edge：MC-064。 | R2/U2/E1 |
| `F7-V19` `core_ir.c:minic_core_function_append_value_instruction` | reserve 后稳定 ValueId/InstructionId，verifier 检查发布值引用。 | R2/U2/E1 |
| `F7-V20` `core_ir.c:minic_core_function_append_effect_instruction` | 带 effect opcode 进 Block 指令，不能伪造 SSA result。 | R2/U2/E1 |
| `F7-V21` `core_ir.c:minic_core_function_set_terminator` | 每 block 单次终结，Core verifier 保证生产块完整。 | R2/U2/E1 |
| `F7-V22` `core/core_lower_metadata.c:core_capture_enum_type_metadata` | Core snapshot complete enum及有效整型表示，owner 不回写 Program。 | R2/U2/E1 |
| `F7-V23` `core_lower_metadata.c:core_capture_enum_metadata` | 跨签名、callee、global、object、value/op type 捕获 enum，commit 之前运行。 | R2/U2/E1 |
| `F7-V24` `core_lower_metadata.c:core_import_fixed_register_binding` | C0 binding 复制到 Core owned metadata；malformed Core API 风险关联 MC-055。 | R2/U2/E1 |
| `F7-V25` `core/core_lower_asm_operands.c:core_inline_asm_symbolic_immediate_name` | GNU `i` 的 FUNCTION/GLOBAL 符号从 AST 解出，缺稳定 FunctionId producer：MC-064。 | R2/U2/E1 |
| `F7-V26` `core_lower_asm_operands.c:core_inline_asm_immediate_text` | 常量或 symbol 转 text，`i/I/rK` 门控；函数身份在文本化时丢失。 | R2/U2/E1 |
| `F7-V27` `core_lower_asm_operands.c:core_inline_asm_specialize_immediates` | `%c0` 展开符号到裸文本，无符号依赖向 Core Reachability 转发。 | R2/U2/E1 |
| `F7-V28` `core/core_lower_asm_scalar_forms.c:core_asm_try_atomic_and_immediate_forms` | 全 immediate 可降为 OPAQUE_INLINE_ASM effect；MC-064 消费路径。 | R2/U2/E1 |
| `F7-V29` `core/core_lower.c:core_source_subtree_membership_from_statement` | 构建 CFG stmt 子树成员，reentry 查询复用，不修改源树。 | R2/U2/E1 |
| `F7-V30` `core_lower.c:core_unreachable_statement_may_reenter` | 外部 goto/已绑定 source label 阻止错误删断后代码。 | R2/U2/E1 |
| `F7-V31` `core_lower.c:core_unreferenced_internal_loop_label` | 只有唯一位置+无外部边的规范 label 才可视为内部。 | R2/U2/E1 |
| `F7-V32` `core_lower.c:minic_core_lower_internal_lower_block` | 断后可重入采用 detached preheader，清 facts，保留 label control edge。 | R2/U2/E1 |
| `F7-V33` `core_lower.c:minic_core_lower_function_with_index` | 独立临时 Core lowering、元数据、verify 成功才替换输出，失败析构不污染旧 Core。 | R2/U2/E1 |

**F7-V 反例边界：** GNU `-O0/-O2` x86 object 保留仅通过 ASM 符号 immediate 引用的 static function；MiniC RV64I 输出 `.quad asm_only_target` 却没有定义 label。加一条普通 CALL 可恢复 label，asm-only 静态全局变量则并无同样问题。未在目标 RV GNU AS/ELF/QEMU 确认最终链接行为，故 MC-064 只记 `ASM-ONLY`。

### F7-P：Frontend 函数、类型、tag 与 Record 字段语义（31 项）

| ID · 源文件与函数 | 生产者／所有权／消费者契约 | 覆盖 |
| --- | --- | --- |
| `F7-P01` `frontend/parser_function.c:record_function_declaration_entity` | Function 声明身份绑定到 Program；兼容性/重声明检验先于下游 Codegen，稳定 FunctionId。 | R2/U2/E1 |
| `F7-P02` `parser_function.c:finish_function_declaration_entity` | 完成旧符号与新定义合并、alias 后处理；不得跨追加借旧 Function*。 | R2/U2/E1 |
| `F7-P03` `parser_function.c:parse_function` | 源 declaration→body/locals 的阶段边界；已核对 Function/Tag 重新获取，未证新 UAF。 | R2/U2/E1 |
| `F7-P04` `parser_function.c:parse_declaration_prefix` | 解析 specifier/storage/attributes 的临时资格，向 function/variable consumer 传稳定类型。 | R2/U2/E1 |
| `F7-P05` `frontend/parser_type.c:minic_parser_parse_type_specifiers` | 枚举/record/tag→稳定 TypeId，返回给 initializer/parameter/record parser。 | R2/U2/E1 |
| `F7-P06` `parser_type.c:minic_parser_parse_pointer_declarator` | 基于完整 base type 递归组合 pointer qualifiers，调用者保存值型 Type。 | R2/U2/E1 |
| `F7-P07` `parser_type.c:type_name_starts_parenthesized_function_pointer` | 词法前瞻仅拷 Lexer+Token，不修改 tags/Program；type-name 分类消费。 | R2/U2/E1 |
| `F7-P08` `parser_type.c:type_name_starts_parenthesized_pointer_to_array` | parenthesized pointer/array 的纯词法 shape probe；失败不提交语义状态。 | R2/U2/E1 |
| `F7-P09` `parser_type.c:parse_abstract_pointer_to_array_type_name` | 生成抽象 pointer-to-array 类型并保留 ArrayTypeId，供 sizeof/cast/parameter 使用。 | R2/U2/E1 |
| `F7-P10` `parser_type.c:minic_parser_parse_type_name_preserving_incomplete` | 允许指针上下文合法 incomplete target，显式标记 incomplete，不提前按值消费。 | R2/U2/E1 |
| `F7-P11` `parser_type.c:minic_parser_parse_type_name` | 完整 type-name 入口，要求 consumer 按对象完整性语义检验。 | R2/U2/E1 |
| `F7-P12` `frontend/parser_function_parameters.c:parse_function_pointer_parameter_declarator` | 参数中的 function pointer+嵌套 tag type 构建，caller 负责参数原型作用域释放。 | R2/U2/E1 |
| `F7-P13` `parser_function_parameters.c:parameter_starts_parenthesized_pointer_to_array` | 参数 declarator 词法分类，不将 lookahead 扩容状态提交给 Parser。 | R2/U2/E1 |
| `F7-P14` `parser_function_parameters.c:consume_prior_parameter_bound` | VLA bound 按先前参数身份消费，不凭未初始化 local fact 猜测。 | R2/U2/E1 |
| `F7-P15` `parser_function_parameters.c:adjust_array_parameter_type` | 数组形参退化为 pointer，元素类型读取后输出值型新类型，无借用跨 arena grow。 | R2/U2/E1 |
| `F7-P16` `parser_function_parameters.c:adjust_function_parameter_type` | 函数形参退化为 function-pointer，保留所指函数签名身份。 | R2/U2/E1 |
| `F7-P17` `parser_function_parameters.c:minic_parser_parse_parameter_list` | prototype/definition 参数 scope owner；作用域边界涉及既有 MC-044 tag leak。 | R2/U2/E1 |
| `F7-P18` `frontend/parser_core.c:minic_parser_begin_scope` | 增加 lexical scope frame，拥有后续 local/tag/enum 绑定可见性窗口。 | R2/U2/E1 |
| `F7-P19` `parser_core.c:minic_parser_end_scope` | 退出 binding frame，删除可见性并保留 Program 永久类型 ID。 | R2/U2/E1 |
| `F7-P20` `parser_core.c:minic_parser_find_nearest_ordinary_binding` | 名称遮蔽按最近作用域查询，不能把 tag namespace 与 ordinary namespace 混用。 | R2/U2/E1 |
| `F7-P21` `parser_core.c:minic_parser_find_tag/_in_current_scope` | tagged Record/Enum 查询区分可见链与本作用域，返回实体稳定 tag identity。 | R2/U2/E1 |
| `F7-P22` `parser_core.c:minic_parser_resolve_tag` | 解析现有 tag 或合法前向引用，消费者只保持 Tag/RecordId 不长期借 Parser 数组地址。 | R2/U2/E1 |
| `F7-P23` `parser_core.c:minic_parser_bind_tag` | 当前作用域绑定名称+kind+entity，析构由 scope owner 管理。 | R2/U2/E1 |
| `F7-P24` `frontend/parser_record.c:record_field_starts_attributed_function_pointer` | GNU record member 属性及指针 declarator 词法 probe，尚有整 Parser copy 性能债。 | R2/U2/E1 |
| `F7-P25` `parser_record.c:parse_pointer_to_array_field_declarator` | 可嵌套 sizeof/匿名 Record 追加，caller 不能复用旧 Record*（MC-061）。 | R2/U2/ASAN |
| `F7-P26` `parser_record.c:parse_function_pointer_field_declarator` | 嵌套函数参数 Record 可重分配 arena，外层字段重复检查同样需重取（MC-061）。 | R2/U2/ASAN |
| `F7-P27` `parser_record.c:parse_record_field_declarator/record_has_field` | 开头借 Record header，递归 declarator 后重复字段验证读过期指针；MC-061 P0 UAF。 | R2/U2/ASAN |
| `F7-P28` `parser_record.c:parse_record_field` | 处理匿名 member/声明 attributes/bitfield；tagged bare Record 被误入匿名成员路径：MC-063。 | R2/U2/GNU-NATIVE-DIFF |
| `F7-P29` `parser_record.c:minic_parser_parse_record_definition_specifier` | 按 tag/匿名 Record 建实体再要求 `{` 完成定义；由前向声明错误分流时触发 MC-067。 | R2/U2/E1 |
| `F7-P30` `parser_record.c:parse_record_forward_declaration/minic_parser_parse_record_definition` | dispatcher 仅识别 `struct IDENTIFIER ;`，漏带 GNU attribute 的合法 forward：MC-067。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F7-P31` `parser_function.c:parser_resolve_pending_function_aliases` | 延迟 alias 基于已登记 function entity 收敛，避免 parser 声明期间借暂时地址。 | R2/U2/E1 |

**F7-P 反例边界：** MC-061 仅指 pointer-to-array/function-pointer 在字段同名检查前创建匿名 Record 的容量相位；直接 array 字段 `field[sizeof(struct {...})]` 此路径在同名检查后扩容，ASan 正控通过。MC-063 是 GNU `struct Named;` 的 *warning-allowed* 源，不是语法错误；x86 GNU native exit48、MiniC native exit88。MC-067 带属性的 forward 五目标全部编译拒绝，去属性五目标全部接受。`MinicParser probe=*parser` 的单纯词法前瞻与 `Lexer+Token` 轻量 probe 并存只记设计债，不能凭复制本身一概宣称内存错误。

### F7-R：RV32/RV64 ABI 分类、实参位置与记录快照（36 项）

| ID · 源文件与函数 | 生产者／所有权／消费者契约 | 覆盖 |
| --- | --- | --- |
| `F7-R01` `target/common/aggregate_shape.c:minic_common_aggregate_storage_shape` | Core type+DataLayout→size/alignment/chunk，RV64 ABI/classifier 消费。 | R2/U2/E1 |
| `F7-R02` `aggregate_shape.c:integral_tree` | 递归 Record/Array 叶子判断整数/pointer，union 中 FP 被误拒的底层判据。 | R2/U2/E1 |
| `F7-R03` `aggregate_shape.c:minic_common_aggregate_integral_tree` | 公共 wrapper 将分类布尔传 RV64 hard-float ABI；MC-059。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F7-R04` `target/riscv64/abi.c:minic_riscv64_classify_abi_value_profile` | LP64/LP64D profile 决定 FP/GPR/indirect；union 不应 flatten：MC-059。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F7-R05` `abi.c:minic_riscv64_classify_abi_value` | 默认 profile wrapper 供 call/return 消费；与显式 ABI option 一致性。 | R2/U2/E1 |
| `F7-R06` `abi.c:minic_riscv64_abi_cursor_initialize` | 8 GPR/FP bank、stack cursor 初始状态；callee/call 两边消费。 | R2/U2/E1 |
| `F7-R07` `abi.c:minic_riscv64_abi_cursor_initialize_for_return_profile` | return profile 初始化保存 register class，不能复用参数增量 cursor。 | R2/U2/E1 |
| `F7-R08` `abi.c:minic_riscv64_abi_cursor_initialize_for_return` | 普通 return 分类 wrapper，Core return emitter 消费。 | R2/U2/E1 |
| `F7-R09` `abi.c:minic_riscv64_abi_variadic_prefix_from_cursor` | named/unnamed 分界 snapshot GPR/stack 位置；`va_start` 消费。 | R2/U2/E1 |
| `F7-R10` `abi.c:minic_riscv64_abi_classify_value_profile` | 参数 role 下 scalar/aggregate/vararg 调用约定，union named 错拒。 | R2/U2/E1 |
| `F7-R11` `abi.c:minic_riscv64_abi_classify_value` | 按类型消费目标 profile，交 caller/callee placement。 | R2/U2/E1 |
| `F7-R12` `abi.c:minic_riscv64_abi_place_argument` | GR/FR/stack 逐项 cursor 更新，保持 ABI word-alignment 不变量。 | R2/U2/E1 |
| `F7-R13` `target/riscv64/function_abi_plan.c:minic_riscv64_function_abi_plan_qualified` | 校验来源 Function/Core signature 与 ABI 合法性；plan 构建入口。 | R2/U2/E1 |
| `F7-R14` `function_abi_plan.c:minic_riscv64_function_abi_plan_build_profile` | OWN 形参/返回/调用布局 plan，错误时由 destroy 收临时分配。 | R2/U2/E1 |
| `F7-R15` `function_abi_plan.c:minic_riscv64_function_abi_plan_destroy` | 释放 call/argument cursor plan 资源；任何失败出口可重入清理。 | R2/U2/E1 |
| `F7-R16` `target/riscv64/call_emit.c:emit_incoming_stack_load64` | 从真实 caller entry SP 载入栈上参数，非当前 realigned SP。 | R2/U2/E1 |
| `F7-R17` `call_emit.c:minic_riscv64_emit_incoming_parameter_object` | register/stack 入参拷入 owned Core Record object，按 ABI chunk 逐字节。 | R2/U2/E1 |
| `F7-R18` `call_emit.c:emit_core_call_argument` | Core arg 与 ABI placement 配对，决定 GPR/FPR/stack 地址或值。 | R2/U2/E1 |
| `F7-R19` `call_emit.c:emit_core_call_arguments` | 保存 call 所有参数的顺序与栈空间，禁止跨 call 复用错误快照。 | R2/U2/E1 |
| `F7-R20` `call_emit.c:rv64_resolve_prepared_call` | CallId/target ABI plan lookup，保证 machine emission 消费同一已验证签名。 | R2/U2/E1 |
| `F7-R21` `call_emit.c:rv64_emit_call_record_result` | record direct/indirect return 复制回 Core 结果对象，不能混淆 sret owner。 | R2/U2/E1 |
| `F7-R22` `call_emit.c:rv64_emit_call_hidden_return_pointer` | 对间接返回提供 caller-owned destination，并按 psABI 占用 GPR。 | R2/U2/E1 |
| `F7-R23` `call_emit.c:minic_riscv64_emit_direct_call` | 直接符号 call 消费 callee profile/arguments/return plan。 | R2/U2/E1 |
| `F7-R24` `call_emit.c:minic_riscv64_emit_indirect_call` | 函数指针 call 用同一 ABI plan，跳转寄存器/RA 由机器负责。 | R2/U2/E1 |
| `F7-R25` `target/riscv32/rv32_abi_plan.c:rv32_plan_shape` | ILP32 word/type shape，区分 empty Record、64b pair、间接对象。 | R2/U2/E1 |
| `F7-R26` `rv32_abi_plan.c:rv32_plan_incoming` | a0–a7 与 entry stack 顺序、跨边界64b 参数 placement。 | R2/U2/E1 |
| `F7-R27` `rv32_abi_plan.c:rv32_plan_call` | caller 参数/return 几何独立计算，对应 incoming 函数 plan。 | R2/U2/E1 |
| `F7-R28` `rv32_abi_plan.c:rv32_function_abi_plan_build` | 将参数身份及布局汇总到 Function ABI plan。 | R2/U2/E1 |
| `F7-R29` `rv32_abi_plan.c:rv32_function_abi_call` | 按 CallId 找到类型和布局，供 rv32_emit_call 消费。 | R2/U2/E1 |
| `F7-R30` `target/riscv32/rv32_call.c:rv32_emit_parameter_object` | a-reg/stack 按 ILP32 chunk 填 Core owned Record slot。 | R2/U2/E1 |
| `F7-R31` `rv32_call.c:rv32_emit_record_return` | record return 依 plan 映射到 a0/a1/sret；既有 MC-057 empty 漏路。 | R2/U2/E1 |
| `F7-R32` `rv32_call.c:rv32_emit_call` | direct/indirect call 与 sret/stack 封送，依函数 ABI plan。 | R2/U2/E1 |
| `F7-R33` `target/riscv64/record_snapshot.c:core_instruction_block_position` | 在 Core block 指令序中确定 value use 的时序位置。 | R2/U2/E1 |
| `F7-R34` `record_snapshot.c:core_call_argument_object_use_count` | 判断 object snapshot 被 Call 使用次数，保护多消费者引用。 | R2/U2/E1 |
| `F7-R35` `record_snapshot.c:core_record_snapshot_single_call_candidate` | 仅唯一 Call、无后续 mutation 才允许 object reuse，非普遍 copy-elision。 | R2/U2/E1 |
| `F7-R36` `record_snapshot.c:minic_riscv64_record_call_result_snapshot_reuse_candidate` | 汇总 per-function candidate 供 RV64 call lowering，不能破坏 Core 多 use。 | R2/U2/E1 |

**F7-R 分类差分：** `MC-059` 的 `union {double;unsigned long long;}` 在显式 `rv64imafd/lp64d` 的 named return/param/direct-call `-S` 错拒、`rv64i/lp64` 五组全部成功；未把 RV64 硬浮点的普通 FP struct 限制误写成 union ABI。R32/64 还补测 packed/bitfield/varargs 组合（`-S` 正控），但第七轮没有目标 GNU AS/QEMU 运行证据。

### F7-A：AArch64 与共用 LP64 的对象、ABI、frame 和机器消费（49 项）

| ID · 源文件与函数 | 生产者／所有权／消费者契约 | 覆盖 |
| --- | --- | --- |
| `F7-A01` `target/common/lp64_aggregate.c:minic_multi_indirect_record` | Common shape→间接 Record 入参/返回；硬上限2048导致 A64/Loong MC-060。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F7-A02` `target/aarch64/abi.c:aarch64_special_aggregate_member` | Recursively exclude unhandled FP/HFA leaves，误把 double* 当 double：MC-066。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F7-A03` `aarch64/abi.c:minic_aarch64_indirect_record` | 叠加 Common indirect 分类与 A64 FP exclusion，向 preflight/return/call 统一提供条件。 | R2/U2/E1 |
| `F7-A04` `aarch64/codegen.c:aarch64_emit_prologue` | 分配 frame、保存 caller SP/LR、x8 sret 与 variadic GP/FP 保存区，供 callee Body。 | R2/U2/E1 |
| `F7-A05` `codegen.c:aarch64_emit_function` | 消费 prepared Core/function plan 发序言→blocks→尾声，失败不发布可用机器产物。 | R2/U2/E1 |
| `F7-A06` `codegen.c:minic_aarch64_write_c0_program_with_core_functions` | TU 全局 section/symbol + per-function 审核/输出，Common writer 入口。 | R2/U2/E1 |
| `F7-A07` `aarch64/call_emit.c:emit_lp64_record_chunk_argument` | 按 Core Record chunks 将寄存器/stack slot 复制到 A64 ABI x-bank。 | R2/U2/E1 |
| `F7-A08` `call_emit.c:aarch64_emit_incoming_stack_read` | 从 saved caller entry SP 读溢出入参，区别当前 aligned frame SP。 | R2/U2/E1 |
| `F7-A09` `call_emit.c:minic_aarch64_emit_abi_instruction` | Core 参数/调用/隐藏 x8 返回指针及 varargs 语义到 A64 registers。 | R2/U2/E1 |
| `F7-A10` `aarch64/block_emit.c:minic_aarch64_emit_lp64_record_chunk_parameter` | callee 把 x-bank/stack chunk 写到 owned local Record 对象。 | R2/U2/E1 |
| `F7-A11` `block_emit.c:minic_aarch64_emit_parameter_object` | Core aggregate PARAMETER_OBJECT 使用已经分类的 ABI placement，避免 C field-layout 重算。 | R2/U2/E1 |
| `F7-A12` `block_emit.c:aarch64_return_gp_record` | small integer/pointer aggregate result 映射 x0/x1，不能误吞 HFA return。 | R2/U2/E1 |
| `F7-A13` `block_emit.c:aarch64_emit_indirect_record_return` | 大 Record→x8 caller-owned destination、字节复制；机器也硬限制2048：MC-060。 | R2/U2/E1 |
| `F7-A14` `block_emit.c:minic_aarch64_emit_terminator` | Core return/branch→A64 regs+epilogue；return shape 由独立 ABI policy 先认可。 | R2/U2/E1 |
| `F7-A15` `block_emit.c:aarch64_emit_gp_result` | value slot/GPR 结果写回 frame，宽度依 Core type。 | R2/U2/E1 |
| `F7-A16` `block_emit.c:aarch64_machine_emit_family` | Common opcode family→A64 integer/FP/memory/call 发射器；不自行发明源语义。 | R2/U2/E1 |
| `F7-A17` `block_emit.c:aarch64_machine_store_gp` | value→physical frame 时保留窄宽度/对齐，不假设每对象都8B。 | R2/U2/E1 |
| `F7-A18` `block_emit.c:minic_aarch64_emit_core_blocks` | 按 Core verified block 顺序发指令/terminator，依 frame plan 管物理寄存器。 | R2/U2/E1 |
| `F7-A19` `target/common/direct_call_plan.c:minic_lp64_plan_types` | 为 Core return/params 建 Common LP64 类型分类数组；失败保留明确原因。 | R2/U2/E1 |
| `F7-A20` `direct_call_plan.c:minic_lp64_plan_incoming_parameters` | callee incoming GPR/FP/stack cursor 和 objects，供序言下沉。 | R2/U2/E1 |
| `F7-A21` `direct_call_plan.c:minic_lp64_admit_call` | 独立检测每个 Core call 的真实 ABI result/argument shape。 | R2/U2/E1 |
| `F7-A22` `direct_call_plan.c:minic_lp64_plan_function_calls` | 构造 CallId→ABI plan，消费所有指令 CallId，不遗漏间接函数指针。 | R2/U2/E1 |
| `F7-A23` `direct_call_plan.c:minic_lp64_function_call_plan` | 只读取回已准备 Call plan，机器层不重跑源类型推断。 | R2/U2/E1 |
| `F7-A24` `target/common/direct_function_profile.c:minic_multi_direct_lp64_profile_function` | 帧、incoming、variadic cursor 与 ABI type 决定 profile，A64 va_list GP/FP bank。 | R2/U2/E1 |
| `F7-A25` `direct_preflight.c:minic_multi_direct_lp64_function_preflight` | 阻止 ABI/type/ASM 不支持形状进入机器发射；过窄时是 MC-060/066 症状。 | R2/U2/E1 |
| `F7-A26` `direct_preflight.c:minic_multi_direct_lp64_cfg_preflight` | 已 verified Core opcode/terminator 对 target emit 能力作最后 fail-closed 审核。 | R2/U2/E1 |
| `F7-A27` `direct_function_plan.c:minic_multi_direct_lp64_function_plan_prepare` | OWN incoming/call/frame 汇总；失败由 destroy 回收半初始化 plan。 | R2/U2/E1 |
| `F7-A28` `direct_function_plan.c:minic_multi_direct_lp64_function_plan_destroy` | 清理 Core-only ABI plan，不能释放 Program/Core producer 所拥有的类型。 | R2/U2/E1 |
| `F7-A29` `target/common/lp64_frame.c:minic_lp64_frame_plan_objects_with_storage` | 为 Core owned objects 在 frame 中分配对齐 storage，传位移给 emit。 | R2/U2/E1 |
| `F7-A30` `lp64_frame.c:minic_lp64_frame_plan_wide` | 宽整数/FP/Record tail 的 slot 几何；与 x86 linear policy 不同。 | R2/U2/E1 |
| `F7-A31` `lp64_frame.c:minic_lp64_finalize_direct_frame` | 汇总 spill/save/stack call size，保留返回指针和 entry-SP 需要的 fixed slots。 | R2/U2/E1 |
| `F7-A32` `target/common/lp64_scalar.c:minic_lp64_scalar_integer_width` | effective enum/整数类型的 width/sign 判断供 A64/Loong register ABI。 | R2/U2/E1 |
| `F7-A33` `lp64_scalar.c:minic_lp64_register64_type` | 整数/pointer 等64b register carrier 的准入，不把 HFA 视 GPR。 | R2/U2/E1 |
| `F7-A34` `lp64_scalar.c:minic_lp64_valid_parameter_type` | ABI 参数类型 admissibility；超过 Common 能力时须准确拒绝而非伪正确。 | R2/U2/E1 |
| `F7-A35` `lp64_scalar.c:minic_lp64_valid_return_with_indirect` | scalar/empty/GP/HFA/indirect 返回分发，x8/Loong sret 各自消费。 | R2/U2/E1 |
| `F7-A36` `target/common/lp64_aggregate.c:minic_multi_gp_record` | 递归整数/pointer Record 分类、field alignment、bitfield 与 DataLayout offsets。 | R2/U2/E1 |
| `F7-A37` `lp64_aggregate.c:minic_multi_fp_return_record` | A64 仅实现特定1/2项 FP return，不能外推支持任意 HFA 参数。 | R2/U2/E1 |
| `F7-A38` `aarch64/memory_emit.c:minic_aarch64_emit_memory_instruction` | Core load/store/atomic 与 volatile memory width→A64 机器形状。 | R2/U2/E1 |
| `F7-A39` `aarch64/fp_emit.c:minic_aarch64_emit_fp_instruction` | F32/F64 算术、NaN ordered compare、FP/int conversion→S/D/Q 寄存器或 slot。 | R2/U2/E1 |
| `F7-A40` `aarch64/integer_emit.c:minic_aarch64_emit_integer_instruction` | Common numeric/overflow shape→ALU/128 carry/shift/窄值 normalize。 | R2/U2/E1 |
| `F7-A41` `aarch64/block_emit.c:minic_aarch64_emit_record_copy` | 精确 byte count 的 Record object copy，大对象转 counted loop。 | R2/U2/E1 |
| `F7-A42` `block_emit.c:minic_aarch64_emit_record_load` | source lvalue→Core owned object byte snapshot，保持 volatile access。 | R2/U2/E1 |
| `F7-A43` `target/common/lp64_aggregate.c:minic_multi_integer_record_members` | 递归 pointer/integer field tree 并做 alignment/bitfield 分类，是 MC-066 的合法 pointer 正控。 | R2/U2/E1 |
| `F7-A44` `lp64_aggregate.c:minic_multi_empty_record` | GNU 0B complete Record 的 call/return 忽略物理数据，不能假造寄存器。 | R2/U2/E1 |
| `F7-A45` `lp64_scalar.c:minic_lp64_valid_return_type` | scalar/0B/GP/HFA type admission，与 indirect-return callback 配对。 | R2/U2/E1 |
| `F7-A46` `aarch64/scalar_normalize.c:minic_aarch64_emit_integer_normalize` | 1/2/4B signed/unsigned 输出归一化，8B 无冗余扩展。 | R2/U2/E1 |
| `F7-A47` `aarch64/frame_emit.c:minic_a64_emit_frame_load/store` | physical width/scratch 传输，caller 负责 record/FP/ABI slot 的 source 类型。 | R2/U2/E1 |
| `F7-A48` `frame_emit.c:minic_a64_emit_frame_address` | 区分 owned SP/x19 和 saved entry SP，Record/variadic/sret consumer。 | R2/U2/E1 |
| `F7-A49` `frame_emit.c:minic_a64_emit_owned_slot` | 8B SSA 或16B Q register carrier 发射，frame owner 管 offset。 | R2/U2/E1 |

**F7-A 边界：** `MC-060` 2049B 大 Record 与 `MC-066` 32B `double*` Record 均已 `-S` 正反测试，不能视为完整 A64 GNU GAS/QEMU。A64 `x8` hidden return、x0–x7/v0–v7 入参和 `va_list` 32B tag state 已追踪 producer/consumer，但仅部分 E2 测试，不能因 R2/U2 声称目标 AAPCS64 已全验收。

### F7-X：x86-64 曾为 R1 的 ASM 长尾再次独立复读（5 项）

| ID · Source → Machine 函数 | 所有权／消费者及证据 | 覆盖 |
| --- | --- | --- |
| `F7-X01` `x86_linux_pm_trace_source` → `emit_linux_pm_trace_record` | `.tracedata` 与行号/relocation GNU/MiniC 原生正控，复核双路径消费。 | R2/U2/GNU-NATIVE-CTRL |
| `F7-X02` `x86_linux_crash_register_source` → `emit_linux_crash_register_snapshot` | GPR/RFLAGS/RIP 快照 Machine 发射与 GNU ASM 核对；跨调用 R11 误报已排除。 | R2/U2/GNU-NATIVE-CTRL |
| `F7-X03` `x86_linux_alt_reloc_selftest_source` → `emit_linux_alt_reloc_selftest` | Source GNU `m` 任意 lvalue vs Machine 固定符号；**MC-062** GNU ELF reloc 正反、MiniC -S 反例。 | R2/U2/LOCAL-COMPILE-DIFF |
| `F7-X04` `x86_linux_real_mode_far_jump_source` → `emit_linux_real_mode_far_jump` | m16:32 远跳转 GNU/MiniC GAS 正控，不能在用户态执行真实模式切换。 | R2/U2/GNU-AS-CTRL |
| `F7-X05` `x86_linux_msr_source` → `emit_linux_msr` | 六种 RDMSR/WRMSR/exception template GNU/MiniC GAS 正控，未执行特权路径。 | R2/U2/GNU-AS-CTRL |

**x86 false positive：** `MC-065` 的 R11 测试对隐式寄存器值没有合法数据依赖，并跨越 caller-saved call，不具备 wrong-code 证明条件。以上 R2/U2 是补齐第五、六轮长尾审阅缺口；其它 x86 special ASM 仍须逐消费者补 E2。

### F7-L：LoongArch64 机器实现的独立阅读（28 项）

| ID · 源文件与函数 | 生产者／所有权／消费者契约 | 覆盖 |
| --- | --- | --- |
| `F7-L01` `loongarch64/codegen.c:loongarch64_emit_prologue` | 保存 caller entry SP/RA、s0 alignment、隐藏返回 a0 与 variadic GPR save。 | R2/U2/E1 |
| `F7-L02` `codegen.c:loongarch64_emit_function` | Consumer 为 prepared Core/ABI/frame plan，emit prologue→blocks→return。 | R2/U2/E1 |
| `F7-L03` `codegen.c:minic_loongarch64_write_c0_program_with_core_functions` | TU 符号/section 全局 writer，调用 Common frame 及 Loong machine。 | R2/U2/E1 |
| `F7-L04` `loongarch64/frame_emit.c:minic_loongarch64_emit_sp_mem` | stack memory offset 超出 immediate 时用 t3 materialize，区分物理位移。 | R2/U2/E1 |
| `F7-L05` `frame_emit.c:minic_loongarch64_emit_sp_adjust` | SP 增减维护16B ABI alignment，callee/call frame consumers。 | R2/U2/E1 |
| `F7-L06` `frame_emit.c:minic_loongarch64_emit_frame_mem` | 依 Common-owned frame slot 地址写物理 load/store，不重算 C type layout。 | R2/U2/E1 |
| `F7-L07` `frame_emit.c:minic_loongarch64_emit_incoming_stack_mem` | 用保存的 entry SP 和必要 t4 scratch 访问溢出参数。 | R2/U2/E1 |
| `F7-L08` `loongarch64/call_emit.c:emit_lp64_record_chunk_argument` | 小 Record 的1..8B chunk→a-bank/stack；复制宽度由 ABI shape 决定。 | R2/U2/E1 |
| `F7-L09` `call_emit.c:loongarch_builtin_immediate_value` | Core 常量/转换至多5层解析，失败保守返回 unsupported。 | R2/U2/E1 |
| `F7-L10` `call_emit.c:minic_loongarch64_emit_abi_instruction` | 入参/直接间接 call、隐式 sret、variadic 与内建参数交界。 | R2/U2/E1 |
| `F7-L11` `loongarch64/block_emit.c:minic_loongarch64_emit_lp64_record_chunk_parameter` | incoming GPR/stack bytes→Core owned object，producer Common plan。 | R2/U2/E1 |
| `F7-L12` `block_emit.c:minic_loongarch64_emit_record_copy` | Core Record VALUE/OBJECT 的精确字节副本，不误当 scalar move。 | R2/U2/E1 |
| `F7-L13` `block_emit.c:minic_loongarch64_emit_record_load` | memory lvalue→snapshot object，volatile 行为交 machine memory emit。 | R2/U2/E1 |
| `F7-L14` `block_emit.c:minic_loongarch64_emit_parameter_object` | per-parameter ABI slot 与 Core ObjectId 对应，caller stack 与 current SP 分离。 | R2/U2/E1 |
| `F7-L15` `block_emit.c:loongarch64_return_gp_record` | small Record return→a0/a1 chunk，引用布局 plan。 | R2/U2/E1 |
| `F7-L16` `block_emit.c:loongarch64_return_indirect_record` | 大 Record sret 复制与硬编码2048 upper bound；**MC-060**。 | R2/U2/E1 |
| `F7-L17` `block_emit.c:minic_loongarch64_emit_terminator` | Core RETURN/BRANCH 的 physical epilogue，依 result ABI type。 | R2/U2/E1 |
| `F7-L18` `block_emit.c:loongarch64_emit_gp_result` | SSA GP 值搬到目标 slot/寄存器，保持 sign-width。 | R2/U2/E1 |
| `F7-L19` `block_emit.c:loongarch64_machine_emit_family` | Common machine family dispatch 至 Loong integer/FP/memory/call。 | R2/U2/E1 |
| `F7-L20` `block_emit.c:loongarch64_machine_store_gp` | 物理 GPR→frame value slot，slot owner 保留字节宽度/对齐。 | R2/U2/E1 |
| `F7-L21` `block_emit.c:loongarch64_machine_emit_terminator` | 经 verified Core block terminator 返回/跳转。 | R2/U2/E1 |
| `F7-L22` `block_emit.c:minic_loongarch64_emit_core_blocks` | 遍历 Core instructions/terminators，交各 family emit，失败 fail-closed。 | R2/U2/E1 |
| `F7-L23` `loongarch64/memory_emit.c:loong_known_address_alignment` | GCD 分析 address origin，packed/指针 conservatism，供 unaligned load/store。 | R2/U2/E1 |
| `F7-L24` `memory_emit.c:loong_emit_scalar_store` | 按地址对齐选择原生 store 或 byte scatter，避免硬件非对齐假设。 | R2/U2/E1 |
| `F7-L25` `memory_emit.c:minic_loongarch64_emit_asm_output_store` | Inline ASM output 由 Core pointer carrier→物理内存写回。 | R2/U2/E1 |
| `F7-L26` `memory_emit.c:emit_integer_memory` | 非对齐整数载入时 byte gather，按 signed/unsigned width normalize。 | R2/U2/E1 |
| `F7-L27` `memory_emit.c:minic_loongarch64_emit_asm_input_scalar` | Source ASM scalar input→Loong register carrier，需保留 qualifier/width。 | R2/U2/E1 |
| `F7-L28` `memory_emit.c:minic_loongarch64_emit_memory_instruction` | Core load/store/atomic/wide 入口，机器发射负责编码和合法性。 | R2/U2/E1 |

**F7-L 范围：** 上述独立阅读涵盖 Loong `codegen.c/frame_emit.c/call_emit.c/block_emit.c/memory_emit.c` 的主要发射及消费约束，着重查 Common/ABI 的真实接点；它们是静态 R2/U2，不伪称逐函数都有单独 GNU Loong GAS/运行时样本。MC-060 是共用 ABI classifier 的受限能力，不能擅自把所有 Loong 机器复制特化都删成 Common 实现。

### 第七轮审计总账

- **212 条 F7 函数/函数组记录**＝P31+C30+V33+R36+A49+X5+L28；R2/U2 是函数级阅读与跨层跟踪，不等于所有路径 E2/机器执行。x86 五个上轮 R1/U1 长尾已升级 R2/U2。
- 新确证 **MC-059/060/061/062/063/064/066/067**（8 项）；其中 MC-061 为 P0 ASan，MC-063 为 P1 GNU native 错码，MC-064 仅 ASM-ONLY，其他以明确 `-S` 接受缺口为主。**MC-065 CLOSED-NOT-A-BUG** 不计入缺陷。第七轮新增的确认编号并不连续，因为需保留误报追踪编号。
- `tests/compiler/audit/` 的输入是修复前预期失败，不能混进 CI 通过率；没有新跨 ISA GNU GAS/ELF/QEMU 完整运行验收。仅源注释 `FIXME` 和文档/fixture，暂不改生产行为。

## 第八轮：最新 `3d504e7ee4a7` 的源码边界与清理消费者补录（2026-10-31）

这轮先以完整 [337 C/H + 两 CLI 的逐文件覆盖账本](minic-source-coverage-f8-2026-10-31.md) 检查文件输入，再沿跨层调用定向 R2/U2。
与先前 `f6e9eb870d80` 源码树逐文件比较，仅 x86 `codegen.c/frame.h/machine_emit.c` 三处变化；
此表的 F8 项**是重审与新交接契约**，不是 F7=212 项之外又新增了这些独立函数。
没有为本表执行新的 ASan、GNU ELF 或 QEMU，`SOURCE` 只说明当前源码路径。

| ID · 生产者 → 消费者（实际源码） | 所有权/不变量/不能直接删除的路径 | 证据与下次验收 |
| --- | --- | --- |
| `F8-01` `compiler.c:minic_compile_preprocessed_file_for_target` | Parser→normalize→integer/symbolic/Boolean→CoreSet→函数裁剪→静态对象裁剪→再次函数裁剪→验证/发射；两次 verify 和两次 prune 各有阶段义务。 | R2/U2/SOURCE；完整顺序见 `compiler.c:264–409` |
| `F8-02` `inline_integer.c` / `inline_symbolic.c` / `inline_boolean.c` | Shared FunctionBody 的 CALL ExpressionId 不具备 caller 实例身份；特化 pass 将私有目标写回共享 AST，见 MC-006/007/008。 | R2/U2/SOURCE + 既存 ELF-DIFF；迁移 `(caller, call)` 事实 |
| `F8-03` `core_lower_calls.c:minic_core_lower_direct_call` / `_direct_record_call_object` | scalar `:746–768` 会作三次 clone selector，record `:909–930` 只读 AST callee；物理返回 object/value 需要不同，callee 选择身份不应分裂。 | R2/U2/SOURCE；尚需 aggregate ELF 正反例 |
| `F8-04` `inline_reference.c:minic_finalize_inline_integer_specialization_references` | 原 source 是否仍须发射依赖被改写后的 CALL；`core_function_set.c:76–97` 在 Core Lower 前据此跳过原 source。不能先删 AST 写入再考虑保活。 | R2/U2/SOURCE；MC-006/064/072 |
| `F8-05` `core_reachability.c` 的 non-Core root 索引与回扫 | Core call/function address、静态对象 relocation、ASM/label 根存在不同阶段；预建索引 OOM 时的逐函数回扫是必要保守 fallback。 | R2/U2/SOURCE；MC-064/072/073 |
| `F8-06` `frontend/cast_normalization.c:minic_c0_program_normalize_casts` | PARSED→NORMALIZED 新表达式 arena 经过 remap 验证后替换旧 arena；这是同一语义 Program 的替换事务，不是两棵常驻 mutable AST。 | R2/U2/SOURCE；排除错误重构前提 |
| `F8-07` `frontend/parser_postfix.c:minic_parser_materialize_array_object_type` | 旧 Local/Record `is_array+count` 需要时新建 ArrayType；正式 `MinicArrayTypeId` 同时存在，所有 consumer 迁移后才能删除桥接。 | R2/U2/SOURCE；A8-01/02 |
| `F8-08` `frontend/parser_global.c` / `parser_static_initializer.c` 的三路数组声明 | 直接 scalar/record 与 preformed typedef 入口重复 begin/merge 属性和 bound 推断，但各有有效 GNU tentative/zero-length/incomplete 语义。 | R2/U2/SOURCE；A8-03 |
| `F8-09` `frontend/parser_attribute.c` semantic probe / `semantic_snapshot.h` | Lexer-only peek 可浅拷贝位置；会扩容 tag/enum/Expression 的语义 probe 没有完整所有权回滚。 | R2/U2/SOURCE + 既存 ASan；MC-004/005/009 |
| `F8-10` `core_lower_loop.c:minic_core_lower_loop_statement` 与 `_loop_cfg` | 普通 while 进入 CFG 前失效化 loop-mutated 特化参数事实；`core_lower.c:1302–1325` 由 referenced LABEL 直接进 CFG，漏该屏障。 | R2/U2/SOURCE；A8-05，**未动态复现** |
| `F8-11` `core_lower_record.c` 与 `core_lower_expr_flow.c` | Record conditional 尚未与 scalar CFG 一样 fork/restore/meet local facts；零/非零 Bool 证明另有不同的保守政策不能直接替换。 | R2/U2/SOURCE + 既存 GNU native；MC-036/019/029 |
| `F8-12` `core_ir.c` / `core_lower_metadata.c` / `riscv64/legalize.c` | enum effective integer metadata 已进 Core；RECORD_LOAD/COPY 的存储 extent/alignment 仍通过 Program/DataLayout 回查。 | R2/U2/SOURCE；A8-06 |
| `F8-13` `target/common/global_emit.c:minic_multi_global_emission_plan_build` | 已统一 payload 分类、NOBITS、对齐与零初始化检查；C0 的 union/bitfield/relocation 逻辑尚由 Common 解释。 | R2/U2/SOURCE；不是常驻 Global IR |
| `F8-14` `riscv64/codegen_function.c:rv64_visit_global` → `global_emit.c` | 当前 target 被丢弃，内部多次用默认 DataLayout/canonical Target；RV32 consumer 则传入 invocation Target。 | R2/U2/SOURCE；A8-08；未证具体布局错码 |
| `F8-15` `riscv32/rv32_global_emit.c` / `riscv64/global_emit.c` | 两目标复用 Common serializer，但仍另行决定 flat/recursive、weak、section、zero；显式 BSS 的 PROGBITS/NOBITS 合同与 Common 不同。 | R2/U2/SOURCE；A8-07，缺 RV ELF oracle |
| `F8-16` `riscv64/frame_layout.c` → `record_snapshot.c` | 每个 Record object 两轮调用同一安全快照候选证明，各次再扫 Core instructions/blocks；应预索引，不可取消唯一使用/未逃逸证明。 | R2/U2/SOURCE；A8-22，复杂度可推导、无基准 |
| `F8-17` `target_info.c:minic_target_info_elf_*_is_nonpreemptible` | x86、RV64、A64 的地址发射会按名字重复扫描 C0 globals/functions；统一的 ELF 链接性质事实尚未跨 Core→Target 固化。 | R2/U2/SOURCE；A8-24，不得回退 MC-075 的 GOT/PCREL |
| `F8-18` `x86_64/codegen.c:x86_head_find_direct_data_globals` | 以 reachable LOAD/STORE 地址回溯建每函数、每 global 布尔决策；`machine_emit.c` 据此更改同 global 的所有 GLOBAL_ADDRESS，混合 per-use 语义待证。 | R2/U2/SOURCE；`3d504e7` 新代码，A8-21 |
| `F8-19` `x86_64/asm_contract.c` → `machine_asm.c` | 109 source families 中 21 typed、88 legacy；typed 模板/variant 防伪是必要二次校验，旧字符串 matcher 需逐族迁完再删。 | R2/U2/SOURCE；MC-050/062 |
| `F8-20` `x86_64/machine_call.c:x86_emit_x87_core_helper` | Core 用 TF helper CALL 表达扩展浮点算术，x86 再根据符号名恢复 x87 操作，IR 身份边界未完全清理。 | R2/U2/SOURCE；A8-11，关联 MC-041/051 |
| `F8-21` `target/common/lp64_aggregate.c:minic_multi_indirect_record` → A64/Loong return | 2048B 是机器实现 cap 不是 psABI；A64 ingress 逐字节展开，Loong 已共用 counted-copy。 | R2/U2/SOURCE + 既存 -S DIFF；MC-060 |
| `F8-22` `loongarch64/residual_emit.c:minic_loongarch64_elided_frame_address_instruction` | 每个候选对全函数检查使用/后继，理论二次方工作量；不应删除安全 eligibility 证明。 | R2/U2/SOURCE；A8-13，缺 benchmark |
| `F8-23` `target/backend.h` → `core_lower_asm_target.c` | 同一 Backend 头放 source GNU ASM qualification 与最终机器 emission；Core 查询 source policy 时暴露整个 emit 接口。 | R2/U2/SOURCE；A8-18，可先拆窄头文件 |
| `F8-24` `target_registration.def` → `target_catalog.c/target_info.c/backend.c` | 三处消费同一 X-macro 注册目录；ISA/ABI/capability 分工有据，不是三份独立 Target 枚举。 | R2/U2/SOURCE；确认当前设计，应保留 |

**F8 盲区与维护约束：** 本轮 337/337 C/H 和两 CLI 完成文件级复扫与快照核对，重点函数上述 R2/U2；
但 Frontend 全部 builtin、两个极长 x86 GNU ASM 模板表、A64/Loong 特例、全部 RV psABI 组合没有逐条新获 E2。
全量源码清点、函数深读和运行正确性是三个不同维度，不能用 F8 表的行数冒充所有函数证明。
若某个本轮候选要长期闭环，先核已存 MC 条目，再补真实目标验证并登记独立根因，不将已有 P0 问题重编号。

## 第九轮：函数级生产消费者与旧实现退役精查（2026-10-31）

仍以 `3d504e7` 源码为准，F9 是对上面已覆盖文件的**纵深增量**，不宣称多读了不同文件或新增已证错编。
`A9-*` 调查及退役条件集中维护于[架构总入口第九轮](README.md#第九轮函数深挖旧路径消费者与新待证线索)，
历史编号仍按 [MC 台账](minic-issue-register.md)。

| ID · 函数/路径 | 生产者 → 真实消费者 / 旧路由 | 静态结论、清理门槛 |
| --- | --- | --- |
| `F9-01` `parser_function.c:minic_parser_parse_gnu_section_attribute` | 旧 `section_attribute_token_is` + section 字符串专用解析无生产 caller；新 parser_attribute attribute-list 路径负责 source 资格。 | `SOURCE`；MC-031，同删私有 helper 与声明、全属性/section 回归。 |
| `F9-02` `parser_type_query.c:minic_parser_parse_alignof_type_value` | 无生产与测试调用；`parser_expression.c:parse_alignof` 消费类型/表达式两种 source form。 | `SOURCE`；MC-031；可删整个旧 `.c` 及 Makefile 项，保留新 parse_alignof。 |
| `F9-03` `parser_global.c:minic_parser_parse_{extern,static}_global` | 零 caller 的 head 前旧 wrapper；有 caller 的 `*_after_head` 是现役解析入口。 | `SOURCE`；MC-031，**只退役前置包装**。 |
| `F9-04` `parser_builtins.c:parse_builtin_va_start` | 先取可扩容 Program `MinicFunction*`，首参递归可由 target machine builtin 扩容 Functions，之后还读 parameter_count。普通 call-arg parser 则先复制 callee header。 | `SOURCE / INVESTIGATE`；需 Loong 变参 + builtin + 容量边界 ASan，不能误称所有 call parser 都 UAF。 |
| `F9-05` `core_lower_local_effects.c:core_{expression,block}_may_modify_local` | 两个 scalar wrapper 只被白盒测试调用，生产 IF/LOOP 已走同内核 batch collector。 | `SOURCE`；先迁测试/私有头，保留共用分类器，再删包装。 |
| `F9-06` `inline_integer.c:minic_specialize_inline_integer_calls` | lazy static_blocked_sources 正常尾部 free，quota/add 失败早退未 free；与现有 MC-043 特化配额错误同路径。 | `SOURCE`；复合 LSan + 单出口清理，失败路径泄漏不冒充成功路径问题。 |
| `F9-07` Boolean→Symbolic→Integer clone constructors | 三 pass 已层层调用同一底层 constructor；匹配器分别多一维 Boolean/符号/整数事实。 | `SOURCE / 排误删`；仅收敛实例、候选遍历和事实身份，不能删除活跃 admission。 |
| `F9-08` `tools/minic` / `tools/minic-cc` option prefix | 两个 CLI 的五类前缀判定逐字相同；前者转发，后者调用目标 option interpreter。 | `SOURCE`；可共享 spelling classifier，不统一 driver/Target 语义责任。 |
| `F9-09` `core_reachability.c:minic_core_named_function_id` | 每次按 Core callee 名称全扫 source function；同文件静态对象符号已建排序/二分索引。 | `SOURCE / PLAN`；预建 TU symbol identity；不得让 Core CALL 依赖仅 C FunctionId，先测热度。 |
| `F9-10` `target_info.c:minic_target_info_elf_*_{nonpreemptible,undefined_weak}` | hidden weak 同时满足两个谓词；A64 residual_emit 明确拒用 direct PCREL，RV64/x86 address lowering 未显式排除。 | `SOURCE / INVESTIGATE`；GNU PIC/ELF、高址、无 provider、GOT/PCREL 对照，分别证明各 ISA；A9-08。 |
| `F9-11` `x86_head_find_direct_data_globals` → `machine_emit.c` | Core 不同 `GLOBAL_ADDRESS` SSA 值可以共享一个 `global_id`；后端按 global 布尔选所有地址的 RIP/VMA。 | `SOURCE`；同符号 mixed-use+helper-only ELF 与 relocated runtime，A8-21 新交叉证据。 |
| `F9-12` RV32/RV64 global visitors → Common GlobalEmissionPlan | RV32 的 default complex 已用 Common Plan，其他 weak/section/zero/reloc/Record 分支可逐个迁移；extern/alias 必须预先分流。 | `SOURCE / PLAN`；按符号绑定、NOBITS 和 data-rel.ro 做真实 ELF 差分，RV `.4byte/.dword` callback 不删。 |
| `F9-13` `riscv64/call_emit.c` + `core_codegen.c` Record byte copy | 两段手写 forward copy 可按 t0/t1/t2/t3 的当前活性契约改用 `riscv/record_copy_bytes.c`；大于 2048B 使用 t3。 | `SOURCE`；先测 1/2047/2048/2049/4097 字节跨 GNU ABI、sret，不当 memmove。 |
| `F9-14` x86 `machine_asm.c` ZERO_BARRIER / FXSAVE / IDENTITY_LOAD | Source form 已能分类，机器 legacy 再按文本选分支；现有 x86 typed form/variant 是真实生产范式。 | `SOURCE`；逐 family 迁 typed、保留 source matcher 与机器防伪，再删 machine 文本重认。 |
| `F9-15` Loong `asm_contract.c` 14 structured descriptors | Source 与 Machine 都查同一 `matches/source/emit` 表，当前还没通过 Core 的 typed form identity 交接。 | `SOURCE / PLAN`；先形成 14 类 ID/variant，再删机器侧重复 registry 扫描；保留不在表中的 legacy。 |
| `F9-16` Loong `call_emit.c:loongarch_builtin_immediate_value` | 机器 builtin fallback 逐次扫描全部 Core 指令找 defining value，而 integer emitter 已用 `fn->values[id].definition` 直接索引。 | `SOURCE`；替换扫描而**保留**缺 `has_constant_integer` 时的 fallback/5 层 conversion 门限。 |
| `F9-17` A64 ASM structured spill guard + movz materializers | 唯一生产 ASM preflight 已限制 value_count，大量重复的 slot 容量 guard 可收敛；多个 A64 文件自写 movz/movk。 | `SOURCE / PLAN`；先固定 preflight 不变量；常量物化共享 ISA helper 但由 caller 管理 scratch/NZCV。 |
| `F9-18` Common Core ObjectReuse + RV64 Record snapshot | Common scalar interval slot 多层扫描复杂度差；RV64 专门证明 Record CALL result 与 by-value 参数对象同址，身份/逃逸条件不同。 | `SOURCE / 排误删`；各做独立函数索引和基准，不能删除 Record snapshot。 |

**F9 留下的边界：** Frontend 10 个零-caller Parser 入口及两个级联 helper 只静态清点，
仍需实际删除后的构建与回归；Core `69/69` opcode switch 的覆盖不等于每类验证正确；
x86、AArch64、LoongArch64 的长 ASM 模板及全部 GNU ABI/QEMU 仍未逐变体实跑。
没有任何本轮源码候选因此提升成 `GNU-NATIVE-DIFF` 或 `VERIFIED`。

## 第十轮：代码退役与两个动态根因回链（2026-10-31）

这是**在 F8/F9 冻结审计之后实施的增量**，不是回写旧基线事实。
涉及真实定义、声明、测试消费者同步迁移；旧编号仍可按 F9 表复盘为什么安全删除。

| F10 项 · 原生产者 / 消费者 | 本轮处理与所有权约束 | 当前证据与仍缺事项 |
| --- | --- | --- |
| `F10-01` F9-01～03、F8 中 `parser_type_query.c` | Parser 的 10 个无调用入口及 2 个级联 private helper，连同 `parser_internal.h` 声明和 Makefile 旧 `.c` 项已真正删除；`parse_alignof`、统一 attribute-list、`*_after_head` 是仍存在的唯一现役消费者。 | MC-031 `FIXED-UNVERIFIED`；`minic-cc` 编译、五组 GNU C attr/section/integer 常量定向正反脚本 PASS；alignof x86 native PASS；未运行全部目标 GNU ELF/QEMU。 |
| `F10-02` F9-04 `parse_builtin_va_start` | `MinicFunction*` 只在递归前读取；`parameter_count` 按值缓存，递归的 Loong machine builtin 即使添加 Program 第 17 个 Function 也不会沿用失效指针。 | **MC-077 ASan 原生 UAF 实证**：旧 `parser_builtins.c:1558` heap-use-after-free，修后 sanitizer 同输入 EXIT0，新增 Loong 专用回归 fixture+脚本；只修当前借用，不顺带判其他 Parser UAF。 |
| `F10-03` F9-05 Core `*_may_modify_local` | 白盒 test 迁入与生产一致的 batch `minic_core_*_collect_modified_locals`，移除两个 scalar 包装与 `core_lower_internal.h` 声明；保留原 effect classifier、OOM conservative path。 | 独立 Core 测试以 `-Werror` 构建且执行 PASS。 |
| `F10-04` F9-06 `minic_specialize_inline_integer_calls` | clone quota/add 失败路径释放 `static_blocked_sources`；不改变 clone constructor、CALL identity 或 boolean/integer 预算。 | 仅资源清理；MC-043 的有效 2050/1025 callsite 失败**仍 OPEN**，专用 LSan 和规模回归待补。 |
| `F10-05` F9-16 Loong `loongarch_builtin_immediate_value` | 以 `fn->values[id].definition` 代替每次线性扫描 Core instructions；仍把定义种类、五层转换、const/fallback 判断留在 Target。 | Loong CSR 四个合法形式/三个非法形式 source-only 定向 PASS；缺交叉 GNU ELF/QEMU。 |
| `F10-06` F8-18/F9-11 x86 `.head.text` | 对 `GLOBAL_ADDRESS(global_id)` 的逻辑符号身份不再当作每个使用点所需的物理/VMA 地址身份；按 SSA producer/consumer 逐步拆开，机器发射保留 ordinary/PIE 非 head 路径。 | MC-076 的原 GNU-NATIVE-DIFF/ELF-ORACLE：原 MiniC 2/2 异址执行 FAIL，GNU 2/2 PASS；本地实现和新回归仍在复核，不声称未知 pointer alias 已修。 |

测试脚本的老式寄存器字符串匹配和已观察到的基线红灯另外回链 MC-021，不把 C 语义通过和
脚本 PASS 混为一谈。本批没有接手 F9-12 RV GlobalEmissionPlan、F9-14/15 typed ASM 大类和
F9-18 ObjectReuse，因为还没有证明删除其活消费者后语义不变。
