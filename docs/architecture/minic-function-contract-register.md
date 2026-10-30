# MiniC 函数级契约与审计台账

> **持续维护，不等于全部函数已审完。** 每个关键函数要回答：谁调用、输入借用还是拥有、可能写什么、返回或失败后谁释放、身份及目标 ABI 不变式是什么。
>
首建：2026-10-30；第四轮复核基线 `400f4a611`，分支 `refactor/minic-target-language-boundary`。配套：[全局阶段与所有权图谱](minic-global-contract-map.md) · [MC 缺陷台账](minic-issue-register.md)。行号仅辅助定位，**函数名和相对路径才是稳定定位符**。

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
