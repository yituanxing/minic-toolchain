# MiniC Core IR / Lower / Target 根目录架构审计（2026-10-29）

**审查基线：** `refactor/minic-target-language-boundary`，起始 HEAD `3866af2f6`；各子代理同时修改其他源码，因此以每个问题给出的路径与行号为准重新定位。**审查方式：** 对 `src/core/` 和 `src/target/` *根目录* 的 C/H 建立逐文件台账；`src/target/common/` 与各 ISA 子目录由其他代理负责。这里的“完整”表示源文件按行阅读过，**不是**表示经形式验证或所有语义路径测试。`定向` 表示已查看入口、关键路径、匹配点，但还没有按行完整通读；绝不将 `rg` 盘点计作已读。

## 当前结论与修复清单

| 严重度 | 证据（原始源码行号） | 问题、影响与建议 | 状态 |
| --- | --- | --- | --- |
| P0/P1 待案例定级 | `src/core/core_lower_asm_target.c:55-62`；`src/core/core_lower_asm_structured.c:578-597`；`src/core/core_ir.h:328`；`src/core/core_ir_verify.c:722` | **target-qualified ASM 丢早期破坏约束**：target contract 分支给 Core operand 写 `kind`、`value` 等字段，却没传递 GNU `&` 约束的 `early_clobber`。通用结构化路径显式填此位，各 ISA 后端多处消费/验证这个位。必须从 source operand 或规范化 contract 提供原始早期破坏信息，先对 `=&r`、`+&r`、输入输出交叉寄存器运行回归，区分目标后端是否已经在规范化前拒绝。不可仅简单假设每个目标都接收这种约束。 | **未修复；已通知总负责人** |
| P0 / source effect 丢失风险 | `src/core/core_lower_const_facts.c:454-485`；`src/core/core_lower_if.c:622-632`；`src/core/core_lower_const_facts.c:961-981` | **NULL-return helper 折叠忽略实参 effect**：`core_expression_known_null_pointer_depth` 若 direct call 的 callee 源函数第一条语句 `return NULL`，就认定整个 call 返回 null，却不检查实参求值纯度。`if (nil(++g)) ...` 由 IF 零指针 shortcut 完全跳过 call/`++g`，改变 C 语义（`!nil(++g)` 也走该 oracle）。写专门 side-effect + `if`/`!` probe；修复需必须保证 call 实参纯度，或保留实参执行后才能折叠结果，并防止 CFG 常量证明跨 side-effect。 | 未修复；已上报总负责人，需立即回归确认 |
| P0 / source effect 丢失风险 | `src/core/core_lower_const_facts.c:525-543,1547-1624`；`src/core/core_lower_if.c:576-640` | **Boolean range 比较恒值推断丢 effect**：`minic_core_lower_boolean_range_expression()` 遇到任何 `_Bool` 类型 expression 都直接宣称只取 0/1；`core_const_eval_integer_with_locals` 对此值和整数 2 等比较可推出恒值，却没有证明这个 `_Bool` 表达式的求值纯度。`if (((_Bool)(++g)) > 1)` / `if (((_Bool)foo()) < 2)` 可以在分支 shortcut 时省略 `++g`/调用。需要补 source-effect gate 或 evaluate-for-effect，和 NULL-return helper 案例一起做 P0 副作用回归。 | 未修复；已报总负责人并建议同批修复 |
| P0 / CFG fact 污染风险 | `src/core/core_lower_expr_flow.c:66-130,308-390,440-575`；`src/core/core_lower.c:740-805`；`src/core/core_lower_assignments.c:147-190`；比较 `src/core/core_lower_if.c:697-818` | **表达式级分支无分支事实合流**：普通 `?:` 在 true/false 两臂间只改 `block_id`，既不将 local facts 恢复到分支入口，也不在 merge 后交集化；`&&`/`||` 的条件 CFG 分裂、值表达式物化亦如此。赋值 expression 会立即对 local facts 写入常量/Null 证明，导致未执行臂的事实可能扩散到 merge。最小测试 `int f(int b){int x=0;b?(x=1):0;return x;}` / `int f(int b){int x=0;b&&(x=1);return x;}`（b=0/1）并与 GNU -O0 差分。参考 `core_lower_if` 的 snapshot、exit facts merge；需要同时保证短路 RHS 不会在 false 分支上污染。 | 未修复；已报总负责人，需基于最小 repro 验证语义 |
| P0 / switch CFG 事实入口污染 | `src/core/core_lower_switch.c:560-602,1060-1250`；`src/core/core_lower.c:1391-1420,1555-1565` | **switch 不同 `case` 入口复用顺序 lower 的 `local_facts`**：上一 case 对 local 写入的 known fact 会保留到后续 case，但后续 case 可被 dispatch 独立进入。新增 `tests/compiler/c0/core_switch_case_fact_isolation.c` 与 GNU 差分：修复前独立 case MiniC exit=2，主路径修复后 6 条通过；嵌套 CASE 路径进一步发现 MiniC exit=8。已按 CFG 前驱在常规 switch 路径对 dispatch entry 与真正 fallthrough/reentry facts 做 meet；嵌套 case/default 由 `core_lower.c` 独立 reentry 标签在入口 fail-closed 清除非稳定 facts。修复后 **9 条测试 GNU/MiniC 均 exit=0**；五目标 `-S`（x86_64/riscv32/riscv64/aarch64/loongarch64）均 exit=0。仅 x86 原生运行时已差分；其余目标运行时仍待整体 CI。 | **动态复现并修复，x86 9/9 PASS；五目标汇编产生 PASS** |
| P1 已有其他代理处理 | `src/target/backend_core_usage.c:124,139,157`（修改前）；`src/core/core_ir.h:148-152` | 使用末尾 `ATOMIC_LOAD` 作为 Core kind 数量上限，遗漏尾部追加的 `VARIADIC_COPY`、`FIXED_REGISTER_WRITE`，导致 usage 诊断与计数静默不全。提议 `MINIC_CORE_INSTRUCTION_KIND_COUNT` 哨兵、遍历时用该常量、测试 enum 家族边界。审查期间其他代理已向上述文件添加该常量并替换计数边界，**尚未由本代理验证测试**。 | **并行修复中** |
| P1 / correctness | `src/core/core_lower_record.c:409-415,436-438,467-485`；`src/target/target_info.c:183-185`；`src/core/core_lower_varargs.c:157-166,197-212` | **RV32 记录型 `va_arg` 游标计算不全**：linear slot=4 字节、record size 最多允许 8 字节，但 record 路径把源游标直接 cast 到 record pointer，再只加 **1 个槽**；标量路径已经有 8 字节对齐和 `ceil(value_size/slot)` 槽数。对 RV32 的 8B/align8 struct 很可能漏读/错移下一参数。应把 linear cursor 的对齐/步长收拢成 Core+TargetInfo 共享 helper，分别按 record ABI 有效长度/对齐调整；加 RV32 连续 `va_arg`（8B record 后跟标量）目标回归。是否现有目标接受此 record C 类型需用测试确认。 | 未修复；已向总负责人报告 |
| P1 / RV32 正确性风险 | `src/core/core_lower_expr_scalar.c:208-315` | **`__builtin_ffsll` 非 LP64 截断**：代码无条件先把 64 位 `long long` 转换到 `unsigned long`，才交给 `INTEGER_CTZ`。注释仅论证 RV64 两者都是 64 位，遗漏 RV32 的 `unsigned long` = 32 位；`__builtin_ffsll(1LL<<40)` 正确返回 41，但高半部可被截掉。应统一使用 `unsigned long long` 或按 target 宽度选择 CTZ 类型，并验证 backend 的 64bit CTZ 实现及 0 输入哨兵；加 RV32 目标回归。 | 静态确认类型逻辑；待动态 target 测试 |
| P1 / 未初始化 IR 元数据 | `src/core/core_lower_expr_scalar.c:1115,1340-1353`；`src/core/core_ir.c:835-864` | **pointer +/- integer 路径拷贝未初始化 `instruction.span`**：`MinicCoreInstruction instruction` 无初值，末尾只写 kind/type/pointer_offset payload，直接 append；IR 容器整体复制结构，导致源 span 非确定性、诊断与 dump 不可复现。以 `memset` 初始化，并补 `.span=expression->span`、`.result=INVALID`；其他 Core builder 审核后统一构造接口以降低同类失误。 | 静态确认缺字段；待 IR dump 测试 |
| P1 | `src/core/core_lower_bool.c:74-158`；`src/core/core_lower_const_facts.c:243-365`；`src/core/core_lower_loop.c:398-449` | **两套独立“零布尔值不会变化”证明**：前者每个查询均扫描整函数全部语句、表达式、ASM；后者为每个 local 缓存扫描结果并有“return 后死语句”的额外逻辑。冗余与结果漂移风险；每条件重复全函数扫描、在深层循环中放大耗时。建议共享一个以 function/local 为键的保守 use/def/escape 摘要，明确两种证明等级，补 goto/reentry、死尾、asm、别名的测试，不能机械删除其一。 | 未修复 |
| P1 / 分层边界 | `src/core/core_lower_asm_operands.c:752-819`；`src/target/target_info.c:1249-1280` | Core 的 GNU immediate 专项代码直接辨认目标约束 `"I"` / `"rK"`，还在 Core 中硬编码 `rK` 允许 `0..31`，而 `I` 才查询 `TargetInfo`，构成 ISA 约束语义泄露和未来第六 ISA 的非法复用风险。让 TargetInfo/target ASM 约束分类器返回 `numeric immediate range`、`symbolic relocation`、`deferred` 等意图，Core 只做纯表达式求值和文本替换。 | 未修复；已向总负责人报告 |
| P1 / Core 编排耦合 | `src/core/core_lower_asm_structured.c:17-208,216-437` | `core_inline_asm_rewind_dead_local_assignment` 为 Linux RISC-V `csr_swap`/`rK` 立即数输出特例匹配 **7 或 8 条具体 Core instruction 形状**，然后直接缩短 block/function instruction 与 value 计数。这是通用 ASM 导入器中的 IR peephole 和特定 ISA/内核惯用法强耦合，一旦临时对象 materialization 改变、Core SSA 用法增多很容易失效。建议先将策略改成 target contract 层的“立即数 vs 寄存器”计划与通用 DSE/use-def 优化，回卷改为能证明没有 live use 的独立 pass；必须保护现有 csr_swap 回归。 | 未修复；已向总负责人报告 |
| P1 / ASM 重复分支 | `src/core/core_lower_asm_scalar_forms.c:89-777,1027-1201`；`src/core/core_lower_asm_structured.c:541-750` | 一个 `"r"/"rK"` 单输出单输入、立即数输入、两输入、`+A` 内存读改、空模板等在各类 scalar/structured handlers 重复出现多套 preflight + commit + `REGISTER_OUTPUT_INLINE_ASM`/`STORE` 或 `STRUCTURED_INLINE_ASM` 构造。分支按调用先后优先级择一，未来新增 constraint 难以全局覆盖，也使 `early_clobber`、clobber/effect 语义有分歧。建议抽取 target-decided **AsmOperandPlan**，用一份通用 commit 管道，原有特例仅提供不改变 C 语义的明确计划，不做 duplicate mutation。 | 未修复；整理优先级高 |
| P1 / 位域四份语义实现 | `src/core/core_lower_bit_fields.c:17-310,313-520,523-1110,1113-1502` | 位域简单赋值、读取、复合赋值、`++/--` 四套重复内嵌 DataLayout storage query、low-mask、bit-shift、signed extension、RMW merge、volatile load/store。四处若其中一处改位域 C 规则会漂移。建议拆成共享语义对象 `CoreBitFieldAccessPlan`（field storage/value width、qualifiers、offset）和三项泛用 IR builder：`extract_signed_value`、`truncate_to_field`、`merge_storage`，四种 expression owner 只保留自身的 C 求值顺序。复合赋值 RHS 之后可能修改相邻位域的 second load **不可盲删**。 | 未修复 |
| P1 | `src/target/target_catalog.c:12-84`；`src/target/target_info.c:33-310`；`src/target/backend.c:15-135`；`src/target/data_layout.c` | 架构注册尚有**多份并行穷举表**：catalog 字符串、target 描述符、backend switch、DataLayout 定义。添加第六个 ISA 不必动前端，但必须维护多个存在漂移风险的注册位置。建议每个 target 单独导出 `TargetDescriptor`（数据布局/特征/后端入口），集中一次枚举；如受 CLI/库链接方式限制，可先建校验注册全集的测试，不允许数量或 ABI 脱节。 | 设计债，需统一入口；本轮不冒险整体替换 |
| P1/P2 架构卫生 | `src/core/cfg_constant_calls.c:530-595` | 通用 Core 常量函数折叠器**硬编码 Linux 函数名列表用于无条件 stderr probe**（含 `bio_has_crypt_ctx`、`dax_mapping`、`is_power_of_2` 等），每个命中函数遍历 AST 做 `minic_const_eval_integer` 并 `fprintf(stderr, "CFG_CONST_PROBE ...")`，未受 trace 开关保护。属于遗留调试污染和不必要热路径重算；应删除整个名字白名单 debug 分支或改成明确环境开关下的通用诊断 hook。注意其后的真正 CFG 常量折叠算法是功能代码，不能删。 | 未修复；已通知总负责人 |
| P1/P2 架构卫生 | `src/core/core_lower_if.c:646-662,724-747,807-817,870-878` | IF lowering 对单个 Linux `genl_op_from_split` 符号多次**无条件**打印 `GENL_IF_DECISION`/`GENL_IF_PATH`，是同类硬编码热点诊断残留。去除仅记录日志的名字分支，不影响实际分支判断；其他由 `MINIC_*_TRACE` 环境开关保护的诊断另行低优先级治理。 | 未修复 |
| P2 | `src/core/core_ir.c:529-568` | `minic_core_function_add_callee` 每新增一 callee 都扫描所有先前 inline asm 并复核 `template_text`/`is_volatile`。该不变量在 asm 注册入口及 IR verifier 检查，公共 callee 构造不应依赖 ASM；复杂度 O(callee×asm)。删除重复扫描后保留 verifier 对畸形 IR 的检查，回归 call/ASM 混排。 | 总负责人已确认接手 |
| P2 | `src/target/target_info.c:288-303` | `minic_target_info_for_architecture` 懒初始化静态 `canonical_targets[]`，检查 `data_layout == NULL` 然后写整对象；并发调用会形成非同步读/写。命令行单线程路径下风险低，但作为库的 ABI 需改为静态只读 const、`call_once` 或按请求返回局部完整描述对象。 | 未修复 |
| P2 / 性能 | `src/target/data_layout.c:554-575` 与 record field 辅助布局链 | 程序校验逐 record 逐 field 重新询问布局，深记录/多字段时重复递归深度与偏移检查，存在较多冗余计算。先做深嵌套 record 微基准验证增长曲线，若明显热点改为单次缓存布局，再复用 field 结果。 | 候选优化，尚无 benchmark |
| P2 | `src/core/core_lower_local_effects.c:28-132,133-262`；`src/core/core_lower_if.c:187-224`；`src/core/core_lower_loop.c:384-450` | 为每个 tracked local 重复递归 AST 判断是否有副作用，复杂度近似 `locals × AST`，并与 `local_facts` 的逃逸模型形成隐含契约。优化前需确认所有 address-of 都标记 escape；更稳妥是每块只构造一次写/逃逸集合，供 if/loop 消费。 | 未修复，正确性需补别名/间接写测试 |
| P2 / 失败路径 | `src/core/core_lower_if.c:674-878` | `minic_core_lower_if` 依次 alloc `fallthrough_facts`、`incoming_facts`、`branch_entry_facts`、`then_facts`、`else_facts`，部分 add-block、lower-block、set-branch 错误路径直接 `return`，无统一 cleanup；在批量 C 文件失败重试时泄漏。改成单个 `cleanup:` 统一 free 五个指针、status 返回，保留原始成功 CFG 语义。 | 未修复 |
| P1 / 长双精度 capability 风险 | `src/core/core_lower_assignments.c:370-391`；`src/core/core_lower_arithmetic.c:46-67,317-340,628-644`；`src/target/data_layout.c:42-61` | **不仅 `++/--`**：长双精度 unary negate、binary `+ - * /` 与复合赋值均硬调用 IEEE binary128 `__*tf3`，前两处还直接构造 IEEE binary128 的 `0.0` / `1.0` bit pattern；x86-64 `long double` 虽占 16 bytes，但通常是 x87 extended 80 格式，**大小相同不等于编码相同**。应引入 target scalar *representation/capability* 查询，将 libc helper、常量编码从 Core 搬到 target 语义 provider，未知编码 fail closed。先检查 x86 emitter 是否已拒绝相关 IR，确认实际受影响范围。 | 未修复；需构造分目标回归 |

## 关键边界判断

- `MinicCoreInstruction` 中的 ASM 操作数应传递*约束语义*，而不要把 x86/AArch64/RISC-V 的寄存器名字、machine opcode 或编排算法带入 Core。`src/core/core_lower_asm_target.c` 虽然已通过 target `normalize` / `contract` 做了能力检查，却在导入时丢失 `early_clobber`；这是边界信息缺失而非“Core 要处理 ISA”。
- `TargetInfo` / `DataLayout` 的语义导出是正向结构；接口不应要求前端用字符串枚举五个架构。**“完整目标支持的 ISA”与“MiniC 后端主动产生的最小指令集合”必须继续分开**；勿因为 RV 后端只输出 I 基本集就删掉内联汇编/ABI 对扩展的合法性信息。
- 当前 Core IR 表示有 CFG block、terminator、value definition、structured ASM 等明确概念。它不是通用 φ-SSA：跨控制流的值使用需要 lowering 尤其注意无显式 dominance/φ 的情形。`core_lower_asm_target.c:127-175` 对对象地址跨块进行了有限的本块重物化，且主动拒绝重放带副作用表达式；这段特殊修复提示，应建立**全局使用定义/支配规则和 verifier 检查**，避免继续靠按症状补特殊值。
- `src/core/value_uses.c` 已将 instruction/terminator operand 枚举集中成 helper；以后新增 kind，必须在 verifier、dump、value uses、统计、各 target emission 五处做完整性测试。新增 `COUNT` 仅保证诊断数组尺寸，**不能自动保证所有 switch cases 覆盖**。

## 逐文件审读证据台账

`完整` = 从头到尾通读 C/H；`定向` = 看过代表性代码、调用或结构但未逐行覆盖。文件数基线 **53，约 30,255 行**；将 `定向` 全部补齐前，不得称整个 Core+Target 根目录“100% 全读”。

| 文件 | 行数 | 读审状态与关注点 |
| --- | ---: | --- |
| `src/core/cfg_constant_calls.c` | 677 | 完整：小型 CFG loop 解释器、参数纯度、Linux 专名无条件 probe |
| `src/core/cfg_constant_calls.h` | 12 | 完整：常量 inline call Oracle 接口 |
| `src/core/core_ir.c` | 910 | 完整：容器、callee/ASM 注册、IR 构造验证 |
| `src/core/core_ir.h` | 639 | 完整：kind、payload、value/block、公开 API |
| `src/core/core_ir_dump.c` | 665 | 完整：全部 Core opcode/terminator 文本 dump，verify-before-dump |
| `src/core/core_ir_internal.h` | 14 | 完整：内部边界 |
| `src/core/core_ir_verify.c` | 1229 | 完整：instruction/CFG/ASM 验证约束 |
| `src/core/core_lower.c` | 1751 | 完整：参数入口 ABI、cleanup/return、短路条件分支、label/goto/indirect goto、不可达子树外部重入、Core function 入口与 verify |
| `src/core/core_lower.h` | 22 | 完整：核心 lowering 入口/状态接口 |
| `src/core/core_lower_address.c` | 596 | 完整：对象地址、数组、成员、动态栈及 source eval ordering |
| `src/core/core_lower_arithmetic.c` | 884 | 完整：整数/浮点 unary/binary/compound，long-double tf3 错误泛化风险 |
| `src/core/core_lower_asm.c` | 356 | 完整：ASM 分发、fallback |
| `src/core/core_lower_asm_internal.h` | 96 | 完整：ASM 私有 API |
| `src/core/core_lower_asm_operands.c` | 1569 | 完整：GNU immediate/symbolic、rK 目标语义、operand/asm-goto 模板正规化 |
| `src/core/core_lower_asm_scalar_forms.c` | 1201 | 完整：legacy mixed `+A`、`rK`、输出与 empty ASM 各类重复变体 |
| `src/core/core_lower_asm_structured.c` | 825 | 完整：两阶段 role preflight、rK 七/八指令形状重卷、asm goto |
| `src/core/core_lower_asm_target.c` | 235 | 完整：target-qualified contract 路径 |
| `src/core/core_lower_assignments.c` | 565 | 完整：语义赋值/前后缀更新、long double helper/bits/ABI 边界 |
| `src/core/core_lower_bit_fields.c` | 1502 | 完整：read/write/compound/update 四套 mask/shift/sign extension 与 storage RMW |
| `src/core/core_lower_bool.c` | 199 | 完整：zero-bool 扫描 |
| `src/core/core_lower_calls.c` | 1124 | 完整：direct/indirect/record calls；constant/symbolic/boolean specialization 选择，统一按参数快照 |
| `src/core/core_lower_const_facts.c` | 1702 | 完整：program zero、const locals、合法整数运算、boolean range 恒值证明与 effect 风险 |
| `src/core/core_lower_expr_flow.c` | 658 | 完整：GNU `?:`、三目/逻辑短路 expression CFG、statement expression；缺失 local facts 分支合流 |
| `src/core/core_lower_expr_scalar.c` | 1359 | 完整：builtin bitops/bswap/ffsll、浮点与指针比较、pointer offset；RV32 `ffsll` 截断、pointer-offset span 未初始化 |
| `src/core/core_lower_expression.c` | 1301 | 完整：C0 expression dispatch、atomic/overflow/convert/record effect、discard/statement expression、local escape 与事实失效 |
| `src/core/core_lower_if.c` | 878 | 完整：const IF reentry、branch fact merge、snapshot/失败释放、遗留 genl probes |
| `src/core/core_lower_internal.h` | 314 | 完整：context/跨模块 helper |
| `src/core/core_lower_local_effects.c` | 262 | 完整：源 AST may-write/escape 扫描 |
| `src/core/core_lower_loop.c` | 504 | 完整：loop CFG 生成、for/do-while parser tail provenance、fact 失效恢复 |
| `src/core/core_lower_metadata.c` | 115 | 完整：enum/type/寄存器元数据 |
| `src/core/core_lower_record.c` | 992 | 完整：record copy/value/materialization、linear record va_arg 游标缺陷 |
| `src/core/core_lower_scalar_values.c` | 873 | 完整：integer/bitcast、load/store、CFG 跨块 spill/reload、标量比较、浮点 helper 与 long-double tf3 转换 |
| `src/core/core_lower_switch.c` | 1311 | 完整：case/range dispatch、nested switch/case、switch dead prefix、function label reentry、constant selector、segment fallthrough/termination；case facts 泄漏风险 |
| `src/core/core_lower_types.c` | 185 | 完整：memory scalar + DataLayout 调用 |
| `src/core/core_lower_varargs.c` | 359 | 完整：scalar ABI cursor / linear va_list advance |
| `src/core/local_facts.c` | 272 | 完整：事实快照、escape、清除恢复 |
| `src/core/local_facts.h` | 41 | 完整：事实状态 API |
| `src/core/reentry_index.c` | 221 | 完整：CFG reentry 辅助索引 |
| `src/core/reentry_index.h` | 35 | 完整：reentry 接口 |
| `src/core/source_loop_shapes.c` | 321 | 完整：parser 合成 for/do/while 源形式解析 |
| `src/core/source_loop_shapes.h` | 46 | 完整：规范化 loop 接口 |
| `src/core/value_uses.c` | 207 | 完整：value use switch/terminator operand |
| `src/core/value_uses.h` | 12 | 完整：visit API |
| `src/target/backend.c` | 135 | 完整：backend dispatch |
| `src/target/backend.h` | 97 | 完整：backend 入口/asm contract |
| `src/target/backend_core_usage.c` | 172 | 完整：Core kind 诊断统计（修复发生在阅读期间） |
| `src/target/backend_core_usage.h` | 18 | 完整：统计 API |
| `src/target/data_layout.c` | 1014 | 完整：ABI 对齐与 record 布局 |
| `src/target/data_layout.h` | 81 | 完整：DataLayout 接口 |
| `src/target/target_catalog.c` | 83 | 完整：名字/ISA 枚举注册 |
| `src/target/target_catalog.h` | 57 | 完整：catalog API |
| `src/target/target_info.c` | 1326 | 完整：五 ISA target descriptor，ABI/layout 查询 |
| `src/target/target_info.h` | 233 | 完整：TargetInfo 公共接口 |

## 下一批必须验证/治理

1. `core_lower_asm_operands.c`、`core_lower_asm_scalar_forms.c`、`core_lower_asm_structured.c`：GNU constraints/readwrite/early-clobber/`asm goto` 全入口，对比 verifier 的不变量；新增 early-clobber path regression。
2. `core_lower.c`、`core_lower_expr_flow.c`、`core_lower_if.c`、`core_lower_loop.c`、`cfg_constant_calls.c`：集中核实 reentry、支配与 fact state 合流，测试 indirect goto/statement expressions。
3. `core_lower_record.c`、`core_lower_bit_fields.c`、`core_lower_calls.c`、`core_lower_varargs.c`、`core_lower_arithmetic.c`：为 LP64/ILP32/长双精度做 DataLayout 耦合核对。
4. `core_ir_dump.c`、`value_uses.c`、`core_ir_verify.c`、`backend_core_usage.c`：以 CI 的 kind 家族 probe 保证未来新增 Core op 后消费链完整。

**审读完成度：** `src/core/` 的 43 个 C/H + `src/target/` 顶层 10 个 C/H，**53/53 全部从头至尾通读**（约 30,255 行）。此为**静态源码覆盖度**，不是 53 个文件的逻辑问题全部关闭，也不是五 ISA 动态回归全绿。表中的 P0/P1 一律先按“静态风险待最小回归证实”管理；除非注明动态复现，不能把风险候选当成已经实验确认。架构整改和跨 ISA 回归仍在并行推进。

## 第二轮：Core CFG facts/effects 实施与未改清单（2026-10-29）

本轮只修改 `src/core/core_lower_if.c`、`src/core/core_lower_local_effects.c`，及新增三个专属测试文件；不修改 Target、前端或其他代理拥有的文件。原有 switch/ternary facts 修复不做语义变更。

| 类型 | 源码与闭环 | 真实证据 / 修复后的语义 | 结论 |
| --- | --- | --- | --- |
| P1 源 AST effect summary 漏报 | `src/core/core_lower_local_effects.c:49-74`，`MINIC_STATEMENT_ASSIGN`/`RECORD_COPY`/`RECORD_INITIALIZE` 仅检查直接局部赋值目标及 RHS，遗漏复杂左值的**地址计算**。 | 现在对 `target_expression` 递归，`a[index++] = v`、记录赋值左值地址计算均能将 `index` 标为 may-modify。白盒构造真实 AST 并断言返回值；修复前 scanner 会漏报。 | **静态逻辑缺陷 + AST 单测证实并修复**；未宣称发现独立运行时错误。 |
| P1 GNU stmt-expression effect summary 漏报 | `src/core/core_lower_local_effects.c:242-252`，此前只扫描 `statement_expression.block`，遗漏 parser 从 block 脱出的结果 `result`。 | 现在同时扫描尾值表达式，因此 `({ ...; x++; })` 中 `x++` 不会被遗漏；无尾表达式继续维持既有判断。AST 白盒单测可在旧逻辑下失败。 | **静态逻辑缺陷 + AST 单测证实并修复**；当前 GNU/MiniC 原生样例旧新皆通过。 |
| P1 IF 事实域重复实现 | `src/core/core_lower_if.c:229-262` 旧 `core_local_constants_meet_if_paths` 人工合并 `known`、`escaped`、specialization fields，却只从 then 侧复制 `program_zero_checked` 与 `program_zero_stable`。 | 替换成 `minic_core_local_facts_meet`，与 switch/表达式 CFG 共用一个 meet 语义；合并两路时 `program_zero_checked/stable` 均交集化，任一路未知即不保留稳定证明。原单继续路径、零路径仍用原逻辑。 | **代码审计确认的事实域不一致并修复**，独立运行时误编译未复现。 |

验证命令和范围：

1. 新建 `tests/core/core_local_effect_summary_test.c` 直接编译生产 `core_lower_local_effects.c`，覆盖 ASSIGN、RECORD_COPY 与 stmt-expression tail 三个语义入口；`tests/compiler/c0/run-core-local-effect-summary.sh` 自动执行 AST 单测、GNU x86 原生 vs MiniC x86 原生差分、x86_64/RV32/RV64/AArch64/LoongArch64 五目标 `-S`。结果 **PASS**。
2. 独立新建编译器 `-Werror` 构建 **PASS**。
3. 既有 `probe-core-expr-branch-local-facts.sh`：**25/25 且五目标 -S**；`run-core-switch-case-fact-isolation.sh`：**x86 GNU/MiniC 9/9**；`run-core-cfg-fold-effectful-conditions.sh`：**两条 effect oracle PASS**。上述是专项回归，不代表七路 Linux 总矩阵和非 x86 动态执行已经验收。

尚未整改，按优先级排序：

1. **P1 证明域/别名 soundness：** `core_lower_bool.c`、`core_lower_const_facts.c` 的两套全函数 zero-bool 稳定证明同 ISA 独立实现，尤其要先补 goto/reentry、inline asm、逃逸及 statement-expression 的反例后才合并；不能直接视作等价。
2. **P1 Core 效果与值分离：** CFG 条件常量路径已修 P0 `nil(tick())`、`_Bool tick()`，但更多 source-fold shortcut 需审计“值可证明常量”是否等于“表达式求值可省略”，见本报告 NULL-helper/Boolean range 历史问题（其状态以最新 `review-2026-10-29-core-cfg-effects.md` 为准）。
3. **P2 性能与 ownership：** `core_lower_if.c:187-227` 与 `core_lower_loop.c` 仍对每个 tracked local 遍历源 AST，近似 `O(locals×AST)`；建议未来构造按 block 的 may-write/may-escape 摘要一次并集，避免现在只优化复杂度而弄错保守边界。
4. **P2 错误路径资源释放：** `core_lower_if.c` 对多份 facts snapshot 的 early-return 仍有泄漏；建议统一 `cleanup:` 并覆盖分配失败/嵌套 block lowering 失败回归，不与本轮 CFG 语义变更混作一个补丁。
5. **待大矩阵：** 当前是 focused C0/GNU-native + 五目标汇编生成证明；七路 Linux 全文件、非 x86 QEMU、grouped native 12/12 须由 exact-head CI 单独确认。

## 第二轮加急：packed bit-field 越界/未对齐内存访问 P0（动态复现并关闭）

**根因：** `src/target/data_layout.c:255-256` 的字段布局返回 `byte_offset=floor(field_start_bits/8)` 和 `bit_offset=field_start_bits%8`，这里的 byte offset **不是**所声明 `unsigned int` 所需完整 4B 存储区的承诺。旧 `src/core/core_lower_bit_fields.c` 在 read / simple assignment / compound assignment / prefix-postfix update 四条路径固定以声明的 32-bit/64-bit 整数类型进行全宽 `LOAD`/`STORE`；对于 `packed { u8 pad; unsigned lo:5, hi:17, tail:10; }`，record `sizeof=5`，tail 起始 `byte_offset=3`、`bit_offset=6`、`field_bits=10`，实际上只需 2B，但旧 Core 从 `p+3` 构造了 4B load，越过对象终点 2B。Loong 因其 `ld.w` 自然对齐要求还有未对齐访问 fault 风险。

**修复：** `src/core/core_lower_bit_fields.c` 统一新增 storage window 和 typed byte load/store helper：

- `byte_count=(bit_offset+field_width+7)/8`，查询 `minic_data_layout_type(record)` 得实际 `record_size/record_alignment`，先验证字段真实字节区间在记录内；只要 record base 对齐低于 storage 宽度，或 byte offset 不满足原始 storage 对齐，或从字段地址全宽访问越过 record 终点，则改为每个相关字节独立 `unsigned char` LOAD/STORE，并在 Core 无符号 storage 类型中按字节位移 OR 复原值；其余证据充分的路径继续原始单次宽访存，避免无谓降速。
- 四条入口共享相同内存访问实现，RMW 的掩码、原有 signed bit-field 符号扩展、C compound/prefix/postfix 求值顺序保持不变。`volatile` 的每次窄读/窄写为各 1 次 **byte volatile** 访问，合计数等于字段覆盖的字节数；安全的原全宽访问仍保持一次。未能对齐的 volatile packed 不能同时满足“一次单宽访存”和“不越界/不跨不对齐地址”，故优先 C 语义下的准确覆盖字节，目标 ISA 的更强 bit-field volatile ABI contract 需后续单独判定。
- 使用新 `minic_data_layout_is_little_endian()` 显式目标布局策略；当前 5 ISA 均标注 LITTLE，未知/大端**拒绝**本 bitfield Core lowering，不在 Core 枚举 ISA。零宽/匿名字段由 DataLayout + frontend 拦截，此 helper 仅用于合法的 named nonzero 字段，不改变其布局逻辑。

**能证明的动态与汇编证据：**

1. 新 `tests/compiler/c0/packed_bitfield_guard_page.c`：mmap 两页、第二页 `PROT_NONE`，5B packed record 放第一页最后 5B，验证 high/tail 读写、后缀与复合运算、signed packed tail 的负数符号扩展及 record base 非自然对齐。**旧编译器** x86 原生二进制运行 `exit=139 / SIGSEGV`，GNU -O0 `exit=0`，**修复后 MiniC** x86 原生 `exit=0`。这是**运行时已复现 P0**，不是仅靠静态分析推测。
2. `tests/compiler/c0/run-packed-bitfield-guard-page.sh`：同一 GNU-preprocessed source，五 ISA `-S` 全部 PASS，GNU/MiniC x86 原生双 PASS；Loong generated assembly 的 packed read/write `ld.bu` / `st.b` 可见（包括字节 offset 0 但 record alignment 1 的情况）。
3. 原 `run-packed-bitfield-field-layout.sh` GCC 差分 + 五 ISA `-S` + x86 native return 42 **PASS**；`run-gnu-packed-record-field.sh` PASS；整个 MiniC 工具链使用 `CFLAGS=-Werror` 构建 PASS；`git diff --check` PASS。

**尚不能宣称：** 非 x86 QEMU 真正运行时、七路 Linux 全文件与 grouped native 12/12 尚未在 exact-head CI 验收。Loong/QEMU 目标可继续用该 guard-page 测试配合对应 sysroot 检查。位域 DataLayout 在非自然对齐嵌套 packed aggregate 中的 provenance，以及 volatile bit-field 的具体目标 ABI 访存约定仍应进入后续专业验证。生产代码这轮不触碰 Target machine emission。

### 交叉复核补洞：合法 32-bit 位域跨越 5 字节（本轮继续修复）

- 新复核源 `packed { unsigned first:1; unsigned value:32; }` GCC 接受、`sizeof=5`；旧 Core 只允许 `bit_offset+width <= declared storage_width`，导致整个函数 unsupported，而非正确的 5 字节访问。复核者提供 `tests/compiler/c0/packed_bitfield_cross_word_window.c` 后，Core window helper 在需要时把**存储位操作容器**从 unsigned32 提升为 unsigned64，继续只读写实际 5 字节；C 的 `value_type`、整数提升/有符号位域的 32bit sign extension 仍基于**原类型宽度**，不能被 transport 64bit 错改。
- 四入口均消费新的可扩容 storage window，signed/unsigned 五字节位域的 read、assign、compound、postfix 全部进入运行时回归。`run-packed-bitfield-cross-word-window.sh`：GNU native **exit0**、MiniC x86 native **exit0**，五目标 `-S` **5/5**，并使用 PROT_NONE guard-page 证明没有第 6 字节的越界访问。
- **OPEN，明确 fail-closed：** GCC 同样接受 `{ unsigned first:1; unsigned long long value:64; }` 的 9 字节 packed record；其 65 位临时位窗口无法装入当前 Core 上限 64 位无符号 transport。已新增 `tests/compiler/c0/packed_bitfield_cross_word_64_limit.c` 并验证 GCC syntax PASS；MiniC `-S` **非零拒绝**并报告 `Core IR does not yet support function 'read_nine'`。后续应引入 high/low split transport 或小型字节向量 helper，不能把宽度截成 64 并静默发错码。
