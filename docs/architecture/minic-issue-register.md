# MiniC 缺陷、风险与架构债台账

> **长期维护文档，不是立即修复清单。** 发现问题先记录、核实、分类；是否进入某一批重构另行决定。
>
> 首次建档：2026-10-30；本轮复核基线：`4f516707d`（`refactor/minic-target-language-boundary`）。
> 范围：MiniC 的 Frontend、Compiler、Core、Target、Driver 和验证流程；不包含 MiniAS、MiniPP、MiniLD、NaSeed。

配套阅读：[全局架构与所有权图谱](minic-global-contract-map.md) · [函数契约审计台账](minic-function-contract-register.md)。

## 使用约定

- **编号永久不复用。** 新发现分配新的 `MC-xxx`；同一个根因涉及多个可独立触发的位置，可以分别登记，并注明共同根因。
- **状态与优先级独立。** `OPEN`（确认待处理）、`INVESTIGATING`（证据不完整）、`DEBT`（已证实的维护债，未必有功能错误）、`FIXED-UNVERIFIED`、`VERIFIED`、`CLOSED-NOT-A-BUG`。`P0` 表示影响可能严重，不代表已经动态复现。
- **证据等级独立。** `ASAN`＝运行时内存错误堆栈；`ASAN-POINTER-PAIR`＝开启 `pointer-subtract` 专项插桩后确认跨对象指针算术；`GCC-DIFF`＝与有效 GCC 输入/生成物对照；`GNU-NATIVE-DIFF`＝GNU C 本机执行与 MiniC x86 `-S`→GNU AS/link→本机执行差分；`LOCAL-COMPILE-DIFF`＝GNU C 合法性 + 当前 MiniC `-S` 成败对照（不等于同目标 GCC/GAS）；`ELF-ORACLE`＝真实目标文件差分；`ASM-ONLY`＝生成汇编及地址/指令关系；`SOURCE-TRACE`＝分配、所有权、调用链证明；`CI-LOG`＝实际工作流/脚本结果；`DESIGN-REVIEW`＝设计与消费者核对；`SUSPICION`＝尚缺反例。
- **不以代码行数、长函数、两个数组或两个实现的表象判 bug。** 查清生产者、身份不变式、持有者、消费者、ABI/能力边界，再决定修法。
- **不因修补代码即关闭。** 修复后填写提交、精确 HEAD 回归结果及适用架构；只有证据达到验收条件才能改成 `VERIFIED`。未验证前保持 `FIXED-UNVERIFIED`。
- 审查时先查本台账，避免把已经排除的误判再次作为重构理由。复现材料目前有部分在临时工作区，**尚未纳入仓库的探针须在修复批次中转成永久回归测试**。

## 索引：确认的缺陷与缺口

| ID | 优先级 | 状态 | 证据 | 归属 | 简述 |
| --- | --- | --- | --- | --- | --- |
| [MC-001](#mc-001-符号闭包读取失效的-expression-指针) | P0 | OPEN | ASAN | Compiler | 符号闭包递归后读取扩容前的 Expression 指针 |
| [MC-002](#mc-002-record-字段声明中的悬空指针) | P0 | OPEN | ASAN | Frontend | Record 字段声明递归后引用旧 Record 地址 |
| [MC-003](#mc-003-运行时-record-初始化器中的悬空指针) | P0 | OPEN | ASAN | Frontend | 运行时记录初始化器跨字段复用旧 Record 地址 |
| [MC-004](#mc-004-属性探测使-parser-tags-失效) | P0 | OPEN | ASAN | Frontend | `probe=*parser` 的 tags 容器所有权错误 |
| [MC-005](#mc-005-属性探测使-parser-enum_constants-失效) | P0 | OPEN | ASAN | Frontend | 同一浅拷贝使 enum_constants 容器失效 |
| [MC-006](#mc-006-传递特化污染共享-call) | P0 | OPEN | ELF-ORACLE | Compiler/Core | 调用实例的 callee 被写进共享 AST CALL |
| [MC-007](#mc-007-整数与符号特化无法组合) | P1 | OPEN | GCC-DIFF | Compiler/Core | 特化阶段间丢失调用实例的整数事实 |
| [MC-008](#mc-008-符号特化过度要求整数参数常量化) | P1 | OPEN | GCC-DIFF | Compiler | 符号立即量与运行时整数无法共存 |
| [MC-009](#mc-009-表达式探测回滚遗漏-owned-call-参数) | P1 | OPEN | SOURCE-TRACE | Frontend | `expression_count` 回滚遗失 CALL 溢出参数 |
| [MC-010](#mc-010-loongarch-特殊-asm-写回绕过对齐保护) | P1 | OPEN | ASM-ONLY | LoongArch64 | 13 个特殊结果写回槽未走安全 store helper |
| [MC-011](#mc-011-source-only-的-qemu-结果报告不实) | P1 | OPEN | CI-LOG | CI | Source-only 仍报告 `GNU_QEMU=2` |
| [MC-012](#mc-012-source-only-的-elf-结果报告不实) | P1 | OPEN | CI-LOG | CI | `.s` 字符串检查被表述为 ELF reloc 验证 |
| [MC-013](#mc-013-native-gate-的-测试触发白名单缺口) | P2 | OPEN | SOURCE-TRACE | CI | 修改 review-batch 编排未必触发 native gate |
| [MC-028](#mc-028-rv64-的-int128-移位计数类型限制) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV64 | 合法 GNU C 的宽移位计数导致 MiniC `-S` 拒绝，窄计数可过 |
| [MC-033](#mc-033-core-独立自增自减语句保留过时常量事实) | P0 | OPEN | GNU-NATIVE-DIFF | Core | `x++`/`x--` 语句捷径漏失效化 local fact，实测错误分支 |
| [MC-034](#mc-034-静态-record-设计符查找对无关对象做指针相减) | P1 | OPEN | ASAN-POINTER-PAIR | Frontend | 合法静态 `.x` initializer 触发栈 Record 与 heap records 无关指针相减 |
| [MC-035](#mc-035-运行时范围初始化器借用-expression-跨扩容) | P0 | OPEN | ASAN | Frontend | 合法 GNU range initializer 在 Program Expression 扩容时真实 UAF |
| [MC-036](#mc-036-record-条件表达式的-local-facts-分支合流缺口) | P1 | OPEN | GNU-NATIVE-DIFF | Core | 结构体条件式缺 fact fork/meet，实测 `f(1)` 返回值错 10 |
| [MC-037](#mc-037-loong-特殊-asm-源端与机器端限定符不一致) | P2 | OPEN | LOCAL-COMPILE-DIFF | LoongArch64 | 合法 volatile cpucfg 输出仅因限定符在机器层拒绝 |
| [MC-038](#mc-038-匿名字段设计符后的位置初始化游标错误) | P1 | OPEN | LOCAL-COMPILE-DIFF | Frontend | 匿名字段 `.b=2,3` GCC 接受、MiniC 错报过多位置初始化 |
| [MC-039](#mc-039-运行时嵌套数组递归跨-array-type-扩容借用) | P0 | OPEN | ASAN | Frontend | 合法嵌套数组 `sizeof` 扩 ArrayType 时读失效借用指针 |

## 索引：尚待验证的风险

| ID | 优先级 | 状态 | 证据 | 位置/疑点 |
| --- | --- | --- | --- | --- |
| [MC-014](#mc-014-rv32-变参与过对齐栈帧交界) | P0 风险 | INVESTIGATING | ASM-ONLY | RV32 变参保存区与入口 SP 有潜在间隙 |
| [MC-015](#mc-015-rv64-变参与过对齐栈帧交界) | P0 风险 | INVESTIGATING | ASM-ONLY | RV64 寄存器变参跨到调用者栈时的间隙 |
| [MC-016](#mc-016-线性-va_list-类型准入过宽) | P2 | INVESTIGATING | SUSPICION | RV32/RV64/Loong 接受任意指针作 `va_list` |
| [MC-017](#mc-017-另外两个-parser-语义探测浅拷贝) | P1 | INVESTIGATING | SOURCE-TRACE | 数组 extent 探测的 Parser 副本 |
| [MC-018](#mc-018-aarch64-短分支距离依据基本块数) | P2 | INVESTIGATING | SUSPICION | `cbnz` 距离仅按 `block_count` 估计 |
| [MC-023](#mc-023-静态聚合后向覆写跨递归借用-record) | P1 风险 | INVESTIGATING | SOURCE-TRACE | Backward zero overwrite 的 `Record*` 跨递归持有 |
| [MC-024](#mc-024-common-core-中的-rk-约束特例) | P2 | INVESTIGATING | DESIGN-REVIEW | Common Core GNU `rK` 方言和 Target policy 分工待证 |
| [MC-025](#mc-025-aarch64-公共全局-object-类型伪操作兼容性) | P2 | INVESTIGATING | SOURCE-TRACE | A64 object suffix 与 Common `.type` 固定格式需实测 GAS |
| [MC-027](#mc-027-targetinfo-惰性初始化的并发契约) | P3 | INVESTIGATING | DESIGN-REVIEW | 静态目标描述符惰性初始化有无多线程 API 义务 |
| [MC-032](#mc-032-ast-verifier-内层容器检查顺序) | P3 | INVESTIGATING | SOURCE-TRACE | 非法内部 Program 可能在嵌套形状验证前被索引构建读取 |

已验证非问题：**[MC-026](#mc-026-source-form-id-的实际身份边界)**，状态 `CLOSED-NOT-A-BUG`，保留编号和消费者证据，避免重复误报。

## 索引：确实存在但不宜立即改的架构与验证债

| ID | 优先级 | 状态 | 证据 | 内容 |
| --- | --- | --- | --- | --- |
| [MC-019](#mc-019-两套-bool-恒零证明扫描重叠) | P2 | DEBT | DESIGN-REVIEW | 两种保守策略共享了大量全程序扫描 |
| [MC-020](#mc-020-无条件的-genl-调试输出) | P2 | DEBT | SOURCE-TRACE | 六处 Linux 特定诊断无开关输出 |
| [MC-021](#mc-021-历史测试与脚本可达性) | P2 | DEBT | SOURCE-TRACE | 老负例、历史补丁和 inactive 流程需分类 |
| [MC-022](#mc-022-符号特化回归的-clone-身份断言不足) | P1 | DEBT | CI-LOG | 测试有真实语义覆盖，但日志断言不能证明 clone 身份 |
| [MC-029](#mc-029-core-if-的-local-facts-快照重复实现) | P3 | DEBT | SOURCE-TRACE | IF 专用快照与共享快照算法重复、消费者均可辨 |
| [MC-030](#mc-030-functionbody-名义只读遍历的隐式写入) | P2 | DEBT | SOURCE-TRACE | CALL 溢出参数只读遍历经浅拷贝发生同值写回 |
| [MC-031](#mc-031-parser-name-bound-无消费者的旧-api) | P3 | DEBT | SOURCE-TRACE | Parser 两个内部 helper 仅留定义/声明、无实际生产调用 |
| [MC-040](#mc-040-嵌套-switch-labels-重复全量扫描) | P3 | DEBT | SOURCE-TRACE | 同一 nested switch 先检测再构造，两次收集标签 |

## 条目详情与验收门槛

### MC-001 符号闭包读取失效的 Expression 指针

- **位置：** `src/compiler/inline_symbolic.c:135–333`，第一次明确读取在 `:250`；扩容点 `src/frontend/ast.c:516`。
- **证据：** ASan `heap-use-after-free`，`READ of size 208`。递归闭包保存 `program->expressions[id]` 的地址，递归追加表达式引发 `realloc`，返回后仍执行 `closed = *expression`。单、双符号参数和多个 `PAD_COUNT` 扩容边界均可复现。
- **关联风险：** `inline_symbolic.c:748–825` 在实参循环外借用 `nested` 指针，同样可能跨表达式追加，但此位置尚未独立动态复现（内部 UAF 会先触发）。
- **修复门槛：** 使用稳定 `ExpressionId` 保存子节点身份，并在任何可能追加之后重新获取地址；双参数场景另补外层 `nested` 复测。避免递归层层拷贝 208B Expression，顾及 RV64 自举栈预算。

### MC-002 Record 字段声明中的悬空指针

- **位置：** `src/frontend/parser_record.c:385–443`，`record_has_field()` 在 `:443` 读旧地址；`src/frontend/ast.c:1136` 使 Record arena 扩容。
- **证据：** 带函数指针字段、嵌套 `struct Inner { ... }` 参数类型的有效 GNU C 输入；ASan 在 RV32、RV64、LoongArch64 复现 UAF。x86/AArch64 同一容量边界未触发，**不能据此宣称安全**。
- **修复门槛：** 递归解析前后使用稳定 `RecordId` 再取 Record，测试多种记录预置数量及五目标 ASan。

### MC-003 运行时 Record 初始化器中的悬空指针

- **位置：** `src/frontend/parser_runtime_initializer.c:1374–1398`、`src/frontend/ast.c:2191`。
- **证据：** `struct Outer o = { sizeof(struct Inner { int z; }), 22 };` 在处理首字段时扩容 Record arena，第二字段访问旧 `record`；ASan 在 RV32/RV64/LoongArch64 复现。GCC GNU11 接受该构造。
- **修复门槛：** 跨字段按 RecordId 重新解析地址，或采用已在静态初始化器中使用的、语义可证明安全的 Record 快照；五目标扩容矩阵。

### MC-004 属性探测使 Parser tags 失效

- **位置：** `src/frontend/parser_attribute.c:745–751`、`src/frontend/parser_core.c:937,973`。
- **证据：** `MinicParser probe = *parser` 浅拷贝后解析 `aligned(sizeof(struct Probe { int p; }))`，新 tag 的 `realloc` 仅更新副本、释放原 Parser 的旧 `tags`；ASan 五目标均复现。
- **修复门槛：** 完整语义探测应使用唯一拥有动态元数据的 Parser，明确保存/恢复词法游标和 tag 可见性；不能把词法副本当成语义事务。

### MC-005 属性探测使 Parser enum_constants 失效

- **位置：** 同一 `parser_attribute.c:745–751`；释放在 `src/frontend/parser_enum.c:309`，旧地址读取在 `:114`。
- **证据：** `aligned(sizeof(enum ProbeEnum { A16 = 4 }))` 导致 `enum_constants` 在副本中扩容，原 Parser 随后查询既有枚举常量触发 UAF；GCC 接受，ASan 五目标复现。
- **修复门槛：** 与 MC-004 统一修 Parser 事务所有权，检查 `tags`、`enum_constants`、scope/index 等所有可分配字段；不要逐字段零碎补丁。

### MC-006 传递特化污染共享 CALL

- **位置：** `src/compiler/inline_symbolic.c:874–878` 与 `src/core/core_lower_calls.c` 的候选选择。
- **证据：** 两个 caller 通过共享的 `mid` 函数体传入不同静态 key。GCC x86 目标文件的 `__jump_table` 有两种 key 重定位（`keys+2`、`keys+0xa`）；MiniC 只有一种。反转 caller 顺序，MiniC 保留的地址也反转，说明共享 CALL 被后写者覆盖。
- **根因：** `(caller FunctionId, CALL ExpressionId)` 的实例专属 callee 被降为单个共享 `ExpressionId → callee`。
- **修复门槛：** 共享 AST 保留源码静态关系；实例特化事实表以稳定 `(caller_id, call_id)` 为键，Core 在具体调用实例上选目标。正反顺序均应保留两种 ELF 重定位；不能依靠“生成 clone 数量大于零”作为正确性证据。

### MC-007 整数与符号特化无法组合

- **位置：** `src/compiler/inline_integer.c` 传递特化、`inline_symbolic.c:780–814`、`src/core/core_lower_calls.c`。
- **证据：** `mid(key, k) → leaf(key, k)`，两 caller 分别提供不同静态 key 且 `k=1`。整数阶段建出 leaf 的整数 clone，却没有将事实归到具体 CALL 实例；符号阶段读共享原始 CALL，`transitive=0`，最终 Core 报不支持的 leaf clone。修正探针中的 x86 GNU ASM 为合法 `.balign 8` 后，GCC `-O2 -c` 通过而 MiniC `-S` 失败。
- **修复门槛：** 与 MC-006 共用实例事实模型；联合整数＋符号特化不丢任何一方事实。

### MC-008 符号特化过度要求整数参数常量化

- **位置：** `src/compiler/inline_symbolic.c` 的 `minic_inline_symbolic_integer_inputs_are_closed` 等检查。
- **证据：** `leaf(int *key, int k)` 中 GNU ASM 只要求 `key` 为立即量，`k` 正常运行时使用；GCC `-O2 -c` 成功，MiniC `-S` 拒绝 `leaf`。当前算法要求**所有**整数参数已知才准符号特化。
- **修复门槛：** 按实际立即量操作数和已知事实需求判定，不强制无关整数形参常量化；与 MC-006/007 共同回归。

### MC-009 表达式探测回滚遗漏 owned CALL 参数

- **位置：** `src/frontend/parser_static_initializer.c:3165–3178`、`semantic_snapshot.h:167–194`、`ast.c:499–528,107–111`。
- **证据：** 对数组 designator 的 `sizeof(func(12 个实参))` 试探解析会给 CALL 分配 `overflow_arguments`；恢复较小的 `expression_count` 后，析构只遍历当前节点计数，旧分配失去所有者。GCC 与 MiniC 均接受相应合法输入。
- **证据边界：** 源码所有权链确认；此环境 LeakSanitizer 无有效动态泄漏报告。普通值型 ArrayType/FunctionType 计数回滚并没有同类堆所有权，不应一概否定。
- **测试缺口：** `tests/frontend/semantic_snapshot_test.c` 只手工设置/调整 Program 的计数，不构造拥有 `overflow_arguments` 的真实 CALL；它能验证快照数量门槛，不能反证 owned payload 泄漏。`semantic_snapshot.h` 中原本过度概括“安全回滚”的注释已单独更正，**代码问题仍开放**。
- **修复门槛：** 回滚前释放所有被撤销表达式的 owned payload，或采用显式事务；补异常/失败路径和 LSan 测试。

### MC-010 LoongArch 特殊 ASM 写回绕过对齐保护

- **位置：** `src/target/loongarch64/asm_contract.c` 对 `add_unless`(2)、`sub_positive`(2)、futex RMW(2)、futex CMPXCHG(2)、CMPXCHG(1)、masked CMPXCHG(2)、small XCHG(2)，合计 **13 个逻辑结果槽**。
- **证据：** 对可能不对齐的 packed C lvalue 仍直接发出 `st.w/st.d`，未经过 `src/target/loongarch64/memory_emit.c` 的安全 store helper。现有 packed 回归覆盖其他路径且记录 `QEMU=NOT_RUN`；当前仅确认汇编路径缺保护，**未证明硬件异常**。
- **修复门槛：** 复用现有 `minic_loongarch64_emit_asm_output_store`；确认结果、地址及 scratch 寄存器互不别名；覆盖 packed/对齐两路、交叉 GAS/ELF/QEMU，不改 LL/SC 源原子循环语义。

### MC-011 Source-only 的 QEMU 结果报告不实

- **位置：** `.github/workflows/minic-native-refactor-gate.yml` 以 `MINIC_SOURCE_ONLY=1` 调 LoongArch 脚本；`tests/compiler/c0/probe-loong-linux-{cmpxchg,xchg}-qemu.sh`。
- **证据：** source-only 仅生成 `.s` 并检查助记符，跳过 GNU AS/link/QEMU，却仍输出 `GNU_QEMU=2` 或 `GNU_QEMU_42=2`。独立按相同环境执行确认。
- **修复门槛：** 报告显式标记 `mode=source_only, qemu=NOT_RUN`；四架构 runtime workflow 中实际 QEMU 步骤继续保留，不能误称全项目从未运行 QEMU。

### MC-012 Source-only 的 ELF 结果报告不实

- **位置：** `.github/workflows/minic-native-refactor-gate.yml` 的 `MINIC_SECTION_ONLY=1`；`tests/compiler/c0/probe-linux-global-section-reloc.sh`。
- **证据：** 此模式只检查 `.section/.weak` 等汇编文本，仍报告 `reloc=1`、`relocations=2`，不能解释为已 `readelf -r` 验证。
- **修复门槛：** `source`/`elf` 验证级别进入测试结果，只有真实 ELF 才报告 relocation 验收。

### MC-013 Native gate 的测试触发白名单缺口

- **位置：** `.github/workflows/minic-native-refactor-gate.yml` 的 `paths`；其执行的 `tests/compiler/c0/probe-review-batch-2026-10-29.sh`。
- **证据：** 仅改 review-batch 编排或未列子测试时，不一定命中 native gate 的 path 白名单。
- **修复门槛：** 覆盖该聚合脚本及其测试变更路径，或建立单一可追踪测试触发契约。

### MC-014 RV32 变参与过对齐栈帧交界

- **位置：** `src/target/riscv32/rv32_frame.c:172–185`、`rv32_emit.c:673–679,781–805`。
- **证据：** 有 `aligned(64)` 局部对象的变参函数把原入口 `E` 保存后计算 `S=alignDown(E-frame_size,64)`。剩余 `a1..a7` 的保存区止于 `S+frame_size`，但调用者栈参数从 `E` 开始；两者间可能有 `0/16/32/48` 字节间隙。八个固定整数参数、零剩余 GPR 时同样从 `S+frame_size` 而非 `E` 初始化游标。
- **验证边界：** 目标汇编可直接证明地址关系；目前没有 RV32 GNU/QEMU 动态差分，故保持 `INVESTIGATING`。
- **修复门槛：** 正确衔接寄存器溢出到 caller stack、零 GPR 路径，并以实际交叉工具链/QEMU 检验四类入口 SP 相位。

### MC-015 RV64 变参与过对齐栈帧交界

- **位置：** `src/target/riscv64/frame_layout.c`、`src/target/riscv64/core_codegen.c` 的变参保存区/动态对齐路径。
- **证据：** `a1..a7` 保存区可止于对齐后 `S+208`，但 caller 栈参数起点是原始 `E=S+208+δ`；有剩余 GPR 后跨栈可能错位。
- **排除：** 无剩余 GPR 的 RV64 路径已经读取保存的入口 SP，不存在 RV32 的第二种错误。
- **修复门槛：** 解决跨界后的地址连续性，GNU/QEMU 验证；在动态运行确认前保持“汇编级高置信风险”。

### MC-016 线性 va_list 类型准入过宽

- **位置：** `src/target/target_info.c:548–575`。
- **疑点：** RV32/RV64/LoongArch 当前只检查参数是否指针，普通 `int*` 可被当作 `va_list` 调用 GNU va builtins。x86/AArch64 MiniC 和宿主 GCC 拒绝。
- **缺口：** 尚缺对应目标 GCC 的 canonical `va_list` 类型差分；可能是明确的扩展/准入限制，应先查目标 ABI 和语言契约。

### MC-017 另外两个 Parser 语义探测浅拷贝

- **位置：** `src/frontend/parser_static_initializer.c:3163–3275`、`parser_external_object.c:650` 和 `src/frontend/parser_constant.c:380–435` 的语义表达式解析调用链。
- **疑点：** 全局检索 38 处 `MinicParser probe=*parser` 类浅拷贝，35 处属于不修改拥有型表的词法前瞻，MC-004/005 是 1 处完整语义解析触发的两种 UAF；array extent 的 probe 也会解析真实表达式，`sizeof(struct X {...})` 可能引入 tag/Record 等实体，需核浅复制 owner 与快照门槛。
- **实测边界：** 本轮有效 GNU C `int array_probe[] = {[sizeof(struct X {char c;})] = 1};` 经宿主 GNU C 语法检查和 RV64 MiniC `-S` 均成功；RV64 `MODE=sanitize`、`ASAN_OPTIONS=detect_leaks=0` 未报错。单一样本没有扩容边界，**不能排除**容量相关 UAF，也不能升级为 Bug；下一步用多种预置 tag 容量、有效 GNU initializer 与五目标 ASan 差分。

### MC-018 AArch64 短分支距离依据基本块数

- **位置：** `src/target/aarch64/block_emit.c:378–404`。
- **疑点：** `block_count<=256` 并不在逻辑上保证 `cbnz` 的 ±1MiB 范围，但当前构造有效超长单块测试受容量/ASM 资格限制，**没有可复现汇编失败**。
- **下一证据：** 找到可接受的大块合法 Core 输入，或证明布局上界与块数真正相关。

### MC-023 静态聚合后向覆写跨递归借用 Record

- **位置：** `src/frontend/parser_static_initializer.c:2360–2524`，`overwrite_static_zero_record_value` / `overwrite_static_zero_record_constant`。
- **疑点：** 当前函数从 `program->records[record_id]` 借用 `MinicRecord*`，在循环内调用能递归解析嵌套表达式的 `overwrite_static_zero_field_value`，之后仍访问 `record->fields` / `is_union`。如果其中一个合法嵌套初始化会通过内联 `struct/union` 定义导致 Program.records 扩容，借用地址可能失效。
- **反证边界：** `parse_static_record_constant`、`parse_static_union_constant` 早已使用 `MinicRecord` 按值快照，设计成立。不能因另两函数有快照，直接断言 backward 路径必然可触发；当前没有能触发失效地址读取的动态反例。
- **本轮最小输入：** `struct Outer g={.tail=3,.rec={sizeof(struct{int z;}),5}};` 配合合法嵌套记录定义，宿主 GCC 语法检查与 RV64 MiniC `-S` 成功；`MODE=sanitize` 关闭当前环境不可用的 LSan 后亦无 ASan 告警。它没有制造可复现的 Record arena 扩容失效，保持 `INVESTIGATING`。
- **下一证据：** 构造 GNU backward designated aggregate 加嵌套类型声明，调节 Record arena 容量，以有效 GCC 输入做五目标 ASan；如递归实际不允许添加 Record，则关闭为非问题。

### MC-024 Common Core 中的 rK 约束特例

- **位置：** `src/core/core_lower_asm_structured.c:204–260` 的 `core_asm_try_immediate_output_csr`，对照 `src/core/core_lower_asm_target.c:10`。
- **疑点：** Common Core 直接识别 Linux RISC-V GNU ASM 的 `rK`、固定 output/input/clobber 形状并优先完成立即量特化。项目已有 Target-owned contract/资格机制，需明确这处 source constraint 语义是否可被目标拒绝、是否只属于受控兼容 fastpath。
- **证据边界：** 未发现跨 ISA 错编实例，Core 也没有直接用 `MINIC_ARCH_*` 枚举发码。需要从 `minic_core_lower_inline_asm` 的 probe 顺序追踪真消费者；**不能按出现 `rK` 就宣告架构泄露 Bug**。
- **下一证据：** 验证五目标 target-required admission、fastpath 输入输出等价与退回路径。若属于安全的旧指令约束兼容，标明正当特例即可。

### MC-025 AArch64 公共全局 object 类型伪操作兼容性

- **位置：** `src/target/aarch64/codegen.c:159` 的目标配置 `"%object"`，及 `src/target/common/global_emit.c:1250` 固定输出 `.type ...,@object` 的 simple-global 路径。
- **调用链已确认：** `src/target/common/direct_program_emit.c:53–70` 对非 extern 的真实全局定义直接调用 `minic_multi_emit_simple_global`，此函数没有获得 `%object` 的目标 dialect；目标配置目前仅影响 alias。两种配置不一致确实会走到，但 AArch64 GNU AS 是否接受 `@object` 仍待确认。
- **证据边界：** 这是静态审查发现的接口差异，尚无合法 C→MiniC→AArch64 GNU AS 的失败样本。当前不作为已确认错码。
- **下一证据：** 现有 `tests/compiler/c0/probe-aarch64-pie-extern-got.sh` 已含 static global + 真实 AArch64 cross GCC/GAS/QEMU 步骤，优先在精确 HEAD 跑它并对照 emitter 路径；必要时补已初始化/未初始化对象的差分。如果 GAS 允许固定格式，关闭。不能把 source-only 结果充当 GAS 通过。

### MC-026 Source form ID 的实际身份边界

- **状态：** `CLOSED-NOT-A-BUG`；2026-10-30 本轮由真实读写调用链复核。保留条目用于防止重新误报。
- **位置：** `src/target/backend.c:minic_backend_asm_source_form_id` 与其 `capture_asm_operand_source`、下游 target qualification。
- **先前疑点：** hash 覆盖模板、ordered constraints、flags、clobbers 等，不直接编码某些符号名称/标签文本，曾担心作为缓存键导致不同 CALL/operand 实例冲突。
- **实际消费者：** 全生产源码检索 `source_form_id`：`backend.c` 生成，`core_lower_asm_operands.c` 保存并在可选诊断中打印，`core_lower_asm.c` 仅生成诊断 signature；**没有实例 cache/key/equality 的生产消费者**。`target_machine_form_id` 单独负责目标机器形式；`tests/core/asm_operand_plan_test.c` 仅要求约束变化时来源哈希变化。
- **关闭依据：** 该字段是故意可跨实例重复的来源指纹，不是每个操作数/label 的唯一 ID；遗漏实例名不违反当前契约。未来若新增以其为 key 的消费者，应重新审查其键语义，不能沿用当前结论。

### MC-027 TargetInfo 惰性初始化的并发契约

- **位置：** `src/target/target_info.c:minic_target_info_for_architecture`。
- **疑点：** 若同进程多个线程首次并发使用静态 lazily-built target descriptor，是否存在初始化竞态，应看 API/driver 的明确线程模型。
- **证据边界：** 当前生产 CLI 每次是独立进程，未证明承诺同进程并发调用或存在可复现数据竞争；不能因此判生产 bug。
- **下一证据：** 查找公开并发使用者、线程安全文档及构造路径；需要时 TSAN/并发探针验证。若定义单线程初始化/预先初始化则记录为设计约束。

### MC-028 RV64 的 int128 移位计数类型限制

- **状态：** `OPEN`（已确认有效 GNU C 输入的 RV64 MiniC `-S` 接受缺口）；证据 `LOCAL-COMPILE-DIFF`。**不是**运行时错码的结论。
- **位置：** `src/target/riscv64/scalar_emit.c:101–122,263–290`，对照 `src/core/core_lower_arithmetic.c:490–540`、`src/core/core_ir_verify.c:123–136` 与 `src/target/riscv32/rv32_wide_emit.c:32–67`。
- **复现（基线 `4f516707d`，2026-10-30）：** 对 `unsigned __int128 shift128_left(unsigned __int128 x, unsigned __int128 n) { return x << n; }` 和对应 `>>`，本机 GNU `cc -std=gnu11 -fsyntax-only -x c` 成功。相同 `.i` 用 `build/debug/bin/minic-cc -S ... --target=riscv64-linux -o ...` 失败：`cannot emit RISC-V function 'shift128_left'`。只改 RHS 为 `unsigned long`，RV64 MiniC `-S` 成功；确认是宽 RHS 的目标能力缺口，而非整个 `__int128` shift 未实现。
- **根因：** 左、右移目标发射器均显式拒绝 `minic_type_is_int128_integer(function->values[right].type)`；Core 按 C shift 语义保留 RHS 整数类型；RV32 同族双字 shift 已提供高位计数处理。
- **证据边界与后续：** GNU **宿主机语法接受**和当前 MiniC 的失败对照足以证明此 `-S` 缺口；**尚未**跑 GNU RV64 `-S`、GNU AS/ELF/QEMU，不能称同目标完整差分或运行时错误。修复前后应补左右移、符号/无符号、过宽 count 和五目标能力边界的回归，勿现在贸然改指令算法。

### MC-032 AST verifier 内层容器检查顺序

- **状态：** `INVESTIGATING`；仅针对可能构造的 malformed 内部 Program，当前没有合法源文件触发路径。
- **位置：** `src/frontend/ast_verifier.c:minic_verify_build_array_owners`（约 1317–1393），与 `minic_c0_program_verify_target_detailed` 的索引建立/Record/Function 容器形状校验顺序。
- **疑点：** 加速 owner 索引可能在详细校验 `record->field_count/capacity`、函数参数数组等嵌套存储前迭代它们。若调用者手工构造非规范 Program，或将来新增会破坏其不变式的生产者，验证器也可能在正常返回 `false` 之前读取非法内存。
- **证据边界：** 当前 Compiler 仅传 Parser 构造的 Program，未找到生产路径违反对应容量/指针规则；因此不作为现存错编 Bug。下一步用最小手工构造 Program + ASan 检验公开 verifier 契约，再决定补 shape preflight 还是显式限定输入为结构合法的 Program。

### MC-033 Core 独立自增自减语句保留过时常量事实

- **状态：** `OPEN`，P0；`GNU-NATIVE-DIFF`。这是已在 x86-64 GNU 工具链完整链接、本机执行中复现的**真实控制流错编**；并非仅靠源码猜测。RV32/RV64/AArch64/Loong 的相同输入也都成功 `-S`，汇编里仅保留错误的返回 99 分支，**未在这些目标上执行**。
- **位置：** `src/core/core_lower_expression.c:1237–1246` 对独立 `i++/--i` 表达式语句直接走 `minic_core_lower_scalar_update`；同文件 `:423–435` 的通用 `lower_expression` 入口先执行 `minic_core_local_fact_invalidate`。`src/core/core_lower_assignments.c:233–435` 的更新器只生成 load/算术/store，不自身处理 fact 失效。
- **独立最小复现（2026-10-30，基线 `4f516707d`）：** `int main(void) { int x=0; x++; if (x==0) return 99; return 7; }`。`build/debug/bin/minic-cc --target=x86_64-linux -S input.i -o minic.s` 成功，`cc -no-pie minic.s -o minic` 成功，运行 `minic` 返回 **99**；GNU `cc -std=gnu11 -O0 -x c input.i -o gnu` 运行返回 **7**。MiniC 汇编确实先把递增值写回，又无条件选择旧事实 `x==0` 的分支。负向例 `--` 的相同错判也在联合探针里观察到。
- **根因判断：** statement-only 优化捷径与普通表达式事实失效策略分叉。下一次修复须审计其他有副作用的快捷路径、合流后的事实，而非只让此单一 probe 通过。
- **验收门槛：** 建永久 `x++/x--/++x/--x`、语句形式和嵌套表达式形式、分支真/假两路的 GNU 输出/exit-code 差分；x86-64 本机和 RV32/RV64/A64/Loong 可用目标回归；复验现存 Core CFG 优化及 `continue/goto` reentry 语义。**本轮先记录，尚未修改 Core 实现。**

### MC-034 静态 Record 设计符查找对无关对象做指针相减

- **状态：** `OPEN`，P1，`ASAN-POINTER-PAIR`；在有效源码上已观察到 ASan `invalid-pointer-pair`，**尚未证明输出错编**。
- **位置与链路：** `src/frontend/parser_static_initializer.c:2698–2699` 把 `Program.records[id]` 的 Record 值型头复制到栈上 `record_snapshot`；`:2747` 在 `.field` 设计符下调用 `parse_static_record_designator_path`（约 `:1438–1480`），后者调用 `minic_parser_find_record_field_path` → `src/frontend/parser_member.c:28:search_record_field_path`。该 helper 无条件计算 `(MinicRecordId)(record - parser->program->records)`，但 `record` 此时位于**不相关的栈对象**。
- **触发输入与检测：** `struct Q { int x; }; struct Q q = { .x = 3 };`；GNU C 合法，MiniC 普通 sanitize `-S` 通过。独立构建 `MODE=sanitize BUILD_DIR=/tmp/minic-audit-20261030/pointer-pairs CFLAGS=-fsanitize=pointer-subtract LDFLAGS=-fsanitize=pointer-subtract` 后设置 `ASAN_OPTIONS=detect_leaks=0:detect_invalid_pointer_pairs=2:halt_on_error=1`，RV64 `-S` 真实报 `AddressSanitizer: invalid-pointer-pair`，栈指向 `search_record_field_path:27` ← `parse_static_record_designator_path:1459` ← `parse_static_record_constant:2747`，地址分别来自 `record_snapshot` 栈与 Program.records heap arena。栈 Record 快照本身是为避免 arena 扩容悬空而设计的，不能因这里有 UB 直接删除快照。
- **语义边界：** 静态初始化路径目前只使用返回的 field indices，不使用该处生成的 RecordId；其余成员/offsetof 消费者传入 Program-owned Record。修复要保证仍使用稳定 RecordId，或让不需要 RecordId 的消费者避免该运算；再以 ASan pointer-pair 和多架构目标回归验证，不能把“编译结果看似正常”当作 UB 修复。

### MC-035 运行时范围初始化器借用 Expression 跨扩容

- **状态：** `OPEN`，P0，`ASAN`；已由合法 GNU C 生产输入真实复现 `heap-use-after-free`，不再只是源码推测。
- **位置：** `src/frontend/parser_runtime_initializer.c:457–514:add_runtime_initializer_once_read` 先缓存 `const MinicExpression *value = minic_c0_program_expression(program,value_id)`；`:489` 的 `minic_parser_statement_local_lvalue` 经 `src/frontend/parser_statement.c:15–35` 追加表达式，可能 `realloc program->expressions`，而 `:496` 与 `:510` 仍读旧 `value->span`。
- **真实 ASan 反例（2026-10-30，基线 `4f516707d`）：** `int testcase(int seed) { seed + 0; seed + 1; int target[16] = {0, [1 ... 15] = seed + 1}; return target[1] + target[15]; }`，由 GNU C 接受。`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1 build/sanitize/bin/minic-cc --target=x86_64-linux -S /tmp/minic-runtime-init-uaf-probe/add_expression_statement_002.i -o ...` 报 `heap-use-after-free`，`READ size 48` 指向本函数 `:496 assignment.span=value->span`；先前 `parser_statement_local_lvalue:33` → `minic_c0_program_add_expression:519` → `minic_grow_array:20` 释放旧 arena。容量探针前置 0..128 个无效效果式时大部分 PASS、特定两个 `seed+i;` 使增长边界准确命中，**PASS 不是安全证明**。
- **下一步：** 完整容量矩阵和五目标 ASan；在有副作用的 append 前按值保存 `MinicSourceSpan`，并复核同函数/同模块其他跨递归/跨增量存储借用，再集中修这一类所有权问题。本轮未改 Parser 实现。

### MC-036 Record 条件表达式的 local facts 分支合流缺口

- **状态：** `OPEN`，P1，`GNU-NATIVE-DIFF`；x86-64 已完成 GNU C 正常执行与 MiniC→GNU AS/link→本机执行的同源双端差分，确认实际错编。
- **位置：** `src/core/core_lower_record.c:582–713:minic_core_lower_record_conditional_object` 构建 true/false/merge 三块，两臂依次操作同一个 `context->local_integer_constants`，当前看不到进入 true 臂前的快照、false 臂恢复和 merge 交集。对照 `src/core/core_lower_expr_flow.c` 的标量/void 条件表达式使用 `CoreExpressionFactFork` 进行 fork/meet。
- **最小差分（2026-10-30，基线 `4f516707d`）：** GNU C：`struct S{int v;}; int f(int c){int x=0;struct S a={1},b={2};struct S r=c?({x=1;a;}):({x=2;b;});if(x==2)return r.v+10;return r.v;}`，独立 harness 调用 `f(1)` 与 `f(0)`。`cc -std=gnu11 -O0` 执行得 `f(1)=1, f(0)=12`，返回成功；`cc -E -P` 冻结 `.i` → `build/debug/bin/minic-cc --target=x86_64-linux -S` → GNU `cc -no-pie` 链接均成功，执行得 **`f(1)=11, f(0)=12`**，harness 返回 1。说明假臂 `x=2` 的 fact 漏回收后污染 merge 处 IF 剪枝。
- **修复边界：** 应复用既有分支 fact fork/meet 协议，入口 facts 快照应在求值 condition 完成后；真臂成功后记录 then-exit、恢复 entry 再编译假臂，merge 时取交集，并明确分配失败清理。连同 break/goto/reentry 和 side-effect expression 一起检查，不应对 Record 类型另设计一套不相容的 fact 规则。本轮尚未改 Core 算法。

### MC-037 Loong 特殊 ASM 源端与机器端限定符不一致

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`，限 LoongArch64 特殊 GNU ASM 的**编译接受缺口**，尚无 GNU LoongArch GAS/QEMU 目标运行结果。
- **位置：** `src/target/loongarch64/asm_contract.c:758–823` 的 `cpucfg` source gate 对结果类型先 `minic_type_unqualified`，因此接受 `volatile unsigned int`；机器 emitter 检查 Core 输出地址指针 pointee 时直接与无 cv unsigned int `minic_type_equal`，丢失 qualifier 一致性。类似源/机器配对还存在于 `frametrace` 与 `frametrace_csr`（约 `:2826–2990`），暂以**同类根因一条**跟踪，未独立证明这两个形式的动态失败。
- **复现（2026-10-30，基线 `4f516707d`）：** GNU host `cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only -x c` 接受 `unsigned int volatile_cpucfg(unsigned int index) { volatile unsigned int cfg; __asm__ volatile("cpucfg %0, %1\\n\\t" : "=&r"(cfg) : "r"(index)); return cfg; }`。MiniC `--target=loongarch64-linux -S` 对 `unsigned int cfg` 通过，只改成 `volatile unsigned int cfg` 就报 `LoongArch64: Core instruction/ABI not yet supported by M1 emitter`。`unsigned long v; __asm__ volatile("csrrd %0, 0x7":"=r"(v));` 与 `volatile unsigned long v` 的对应 GNU CSR source 也测得同一组 MiniC PASS/FAIL；按同类 qualifier 根因归入一项。
- **验收门槛：** 对源端 qualifier、Core pointer pointee、机器输出类型定义一个共用类型比对契约；补 cpucfg 和两组 frametrace 的 volatile 与非 volatile 组合，目标 GNU AS/ELF/QEMU 执行后才宣称运行时正确。

### MC-038 匿名字段设计符后的位置初始化游标错误

- **状态：** `OPEN`，P1，`LOCAL-COMPILE-DIFF`；合法 GNU C 在当前 MiniC x86-64 `-S` 错误拒绝，尚无已发射错码证据。
- **最小反例（2026-10-30，基线 `4f516707d`）：** `struct Outer{struct{int a;int b;};int c;}; int f(void){struct Outer x={.b=2,3};return x.b*10+x.c;}`。GNU `cc -std=gnu11` 接受、执行返回 `23`，MiniC `--target=x86_64-linux -S` 报 `too many positional record initializers`。若只初始化 `{.b=2}` 或显式写 `{.b=2,.c=3}`，MiniC `-S` 和 GNU 链接/执行均通过，说明主要缺口是 `.b` 后的**隐式位置续接**，不是匿名成员查找能力。
- **源码候选根因：** `src/frontend/parser_runtime_initializer.c:1513–1579:parse_runtime_record_designator_target` 从提升的最终 `MEMBER` expression 读取 `field_index` 并用作 `top_level_field_index`；但 `src/frontend/parser_member.c:113–195` 对匿名成员返回的最内层 `field_index` 是内层 Record 的索引。`parser_runtime_initializer.c:1658–1679` 将该值 `+1` 作为下一个顶层位置游标，可能跳错位置。
- **验收门槛：** 建带匿名 struct/union、连续/嵌套 designator、隐式位置续接与显式位置设计符的 GNU C 差分；由明确的顶层路径第一段确定游标，不依赖最后一个递归 MEMBER；多目标 `-S` 与可执行语义测试后再关闭。

### MC-039 运行时嵌套数组递归跨 ArrayType 扩容借用

- **状态：** `OPEN`，P0，`ASAN`；已由合法 GNU C 的真实 Parser 路径和精准容量边界测得 UAF。
- **位置：** `src/frontend/parser_runtime_initializer.c:647–742:parse_fixed_runtime_nested_array_initializer` 在 `:668–674` 保存 `const MinicArrayType *nested_array` 指向 `Program.array_types[]`；循环中 `:706–707` 递归 parse 子数组，随后继续读 `nested_array->element_type/element_count`。合法嵌套初始化器表达式如 `sizeof(int[777])` 可以向 `array_types` 追加 descriptor，经 `src/frontend/ast.c:1273–1298` `minic_grow_array` 使旧地址无效。
- **真实 ASan 反例（2026-10-30，基线 `4f516707d`）：** 以下是精确输入，共 14 个前置不同 ArrayType 类型。GNU `cc -std=gnu11 -fsyntax-only -x c` 接受：

  ```c
  int testcase(void) {
      (void)sizeof(int[3]);  (void)sizeof(int[4]);
      (void)sizeof(int[5]);  (void)sizeof(int[6]);
      (void)sizeof(int[7]);  (void)sizeof(int[8]);
      (void)sizeof(int[9]);  (void)sizeof(int[10]);
      (void)sizeof(int[11]); (void)sizeof(int[12]);
      (void)sizeof(int[13]); (void)sizeof(int[14]);
      (void)sizeof(int[15]); (void)sizeof(int[16]);
      int a[2][1] = {{sizeof(int[777])}, {5}};
      return a[0][0] + a[1][0];
  }
  ```

  `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 build/sanitize/bin/minic-cc --target=x86_64-linux -S /tmp/minic-nested-array-uaf-probe/sizeofs_014.i -o ...` 命中 `heap-use-after-free`、`READ size 72`，读取为 `parser_runtime_initializer.c:697:nested_array->element_type`；先前 `sizeof(int[777])` 经 `parser_type.c:890` → `declaration_sema.c:54` → `ast.c:1304` 扩容 `Program.array_types`，由 `minic_grow_array:20` 释放旧 arena。其他前置容量样本 PASS 并不能抵消该反例。
- **后续：** 全五目标 ASan、二层/多层嵌套和包含多种子元素类型的扩容矩阵；递归前按值缓存 ArrayType 描述符或必要的 `element_type`/`element_count`，保持稳定 ArrayTypeId 语义，本轮尚未修复。

### MC-019 两套 Bool 恒零证明扫描重叠

- **位置：** `src/core/core_lower_bool.c:39–198`、`core_lower_const_facts.c:95–394`。
- **事实：** 两者都沿全程序语句、表达式、ASM 输出检查恒零，算法明显重复且均活跃。前者服务 `core_lower_if.c` 的严格 CFG 剪枝，后者允许经过 reentry 检查的 `return` 后不可达写入，以维持 `local_facts`。
- **重构门槛：** 可抽取共同 proof oracle 和可达性/死尾策略，但保留不同的保守程度，不能机械删去任意一路；未发现因此错编。

### MC-020 无条件的 GENL 调试输出

- **位置：** `src/core/core_lower.c`、`src/core/core_lower_if.c` 中六处 `genl_op_from_split` 相关打印。
- **事实：** 普通编译该命名函数可以产生六处不受环境开关控制的诊断输出，影响日志可读性。
- **清理门槛：** 先区分无条件 debug 与 `CORE_FAST_TRACE` 等故障诊断，再统一可开关策略；不应全部删除诊断。

### MC-021 历史测试与脚本可达性

- **位置：** `tests/compiler/c0/run-array-bound-constant-expressions.sh`、`tools/dev`、`tools/ci` 及历史 workflow。
- **事实：** 老 `invalid_runtime_array_bound.c` 实际是合法 VLA，但相关入口仅在 disabled/历史流程中发现；不能误报当前 CI 因此失败。仓库有大量一次性补丁和 PR 阶段脚本，但活跃 workflow 静态可达图不覆盖 Makefile/dynamic 调用，**未被图命中不等于死代码**。
- **清理门槛：** 建立 workflow→Makefile→脚本→fixture 的完整可达性报告，先归档历史证据再删除，禁止批量按文件名前缀清理。

### MC-022 符号特化回归的 clone 身份断言不足

- **位置：** `tests/compiler/c0/probe-inline-symbolic-shared-body.sh`。
- **事实：** x86 native 确实执行多个 caller 并检查不同返回值；**不是纯 grep 假绿**。但脚本只从 `transitive>0` 推出并输出 `transitive_clones_distinct=1`，没有核实各 caller 对应 clone 身份。
- **验收门槛：** 增加 MC-006 的正反顺序 ELF relocation oracle 和 Core callee 身份断言；保留现有动态语义测试。

### MC-029 Core IF 的 local facts 快照重复实现

- **状态：** `DEBT`，不是功能 Bug。`src/core/core_lower_if.c:280–300:core_runtime_if_copy_facts` 与 `src/core/local_facts.c:60–91:minic_core_local_facts_snapshot` 都根据 `source_function->local_count` 分配并 `memcpy` 相同结构，空表/失败均返回 NULL。
- **生产消费者：** IF 专用函数在 `core_lower_if.c` 两个分支直接消费；同一模块另外多处使用共享 snapshot。两条都可达，不是可以直接删除的死函数。
- **重构门槛：** 统一调用共享快照前，核对两处失败时的保守路径、快照生命周期及条件合流；完成分支行为差分后再删旧 helper，避免仅因“几行重复”导致事实传播策略漂移。

### MC-030 FunctionBody 名义只读遍历的隐式写入

- **状态：** `DEBT`，`SOURCE-TRACE`，尚未证明生产错码或现实并发竞态。
- **位置：** `src/frontend/function_body.c:91–104:visit_expression_child_ids` 浅复制 `MinicExpression`，调用 `src/frontend/ast_traversal.c:69–86:minic_c0_expression_visit_child_id_refs`；后者对每个 CALL 参数执行 `minic_c0_call_set_argument`。当参数超过 inline 容量时，copy 的 `overflow_arguments` 指针仍别名 Program owned heap，名义只读的 visitor 会把相同 ID 写入原 buffer。
- **契约边界：** 当前输入可写、值不变，函数在普通单线程 Compiler 中没有证明语义错误；但 `const MinicExpression*` 不保证物理只读，不能未经修改用于共享 AST 的并发只读遍历。
- **后续：** 如果引入并行 read-only pass，先实现真正只读 child visitor，或至少为浅复制的 CALL overflow 提供独立缓冲；不要删除通用可写重映射 visitor，它仍承担 AST normalize 的实际写入任务。

### MC-031 Parser name_bound 无消费者的旧 API

- **状态：** `DEBT`，可延后清理。`src/frontend/parser_core.c:minic_parser_name_bound`（约 697–708）只有 `src/frontend/parser_internal.h:475` 声明，生产/测试仓内无调用者；实际同 scope 冲突检查使用 `_in_current_scope` 版本，普通表达式还通过 `minic_parser_find_nearest_ordinary_binding` 实现遮蔽关系。
- **第三轮新例：** `src/frontend/parser_type_query.c:minic_parser_parse_alignof_type_value` 也只有定义与 `parser_internal.h` 声明，暂无内部生产/测试调用；实际 `src/frontend/parser_expression.c:parse_alignof` 同时支持类型名和表达式这两种 source 形式。两处都是可能的迁移残留，**属于同类“无消费者私有 helper”清理债，不是两项语义 Bug**。
- **后续：** 删除前核对编译目标及可能的外部私有头使用；若无契约消费者，连同声明一并移除。**不应顺手删除** `_in_current_scope`、scoped/global 查询或活跃的 `parse_alignof`，它们仍有真实消费者。

### MC-040 嵌套 switch labels 重复全量扫描

- **状态：** `DEBT`，P3，`SOURCE-TRACE`，不是已证语义错编。
- **位置：** `src/core/core_lower_switch.c:707–725:minic_core_lower_switch` 为识别 nested cases 调用 `core_collect_nested_switch_labels`，随后在 `lower_nested_case_switch`（约 `:431`）再次进行同一类别的收集；两个真实生产消费者均在同一 lowering 时序。
- **建议：** 后续性能/结构整理可把第一次收集结果交给 nested 路径，减少额外遍历，但需保留 nested switch 的 reentry、scope 和合法 fallthrough 规则，不能机械把 nested case lowering 并入普通 switch 分支。

## 已核对并排除的误判（不要在未有新证据时重新打开）

| 项目 | 结论及当前依据 |
| --- | --- |
| `FunctionId` / `ExpressionId` 对应数组下标 | **有意稳定身份**；已发表的 AST ID 准确标识对象，不是两个数组碰巧同序。错误是跨实例写共享 CALL，不是使用整数索引。Core 另有严格尾部事务回退，不能把 AST 的正常 ID 契约机械扩展到所有暂态 Core ID。 |
| RV32 CALL ordinal 的并行数组 | 由同一顺序构造、单调 ID 检查及查找时 `instruction_id/prepared` 校验保证，设计成立。 |
| Core IR append 中的 Block 指针 | 期间增长的是 Instruction/Value/Block 自有 ID 列表，不是 Blocks arena；当前未发现可达 UAF。 |
| Core switch 退出后的 local facts | 唯一调用方在 switch 前后清理已知事实，不会泄漏最后一项 case 的事实。 |
| 静态 Record 初始化器快照 | `MinicRecord record_snapshot` 保存完整的值型头，完成后的字段数组单独分配且保持稳定；实测无对应 UAF。 |
| 词法前瞻的 Parser 浅拷贝 | 38 处中 35 处当前仅做词法/字符串/属性列表前瞻，不修改 owned metadata；不应禁止所有浅拷贝。 |
| x86 Typed/Legacy ASM 并存 | 机器形式资格与实际消费者都存在，属于受控迁移；AArch64 旧全文本 legalizer 已退出当前生产路径。 |
| RV32/RV64 Common GPR cursor | 仅复用八寄存器 word-placement，ILP32/LP64 frame、参数宽度、具体 ABI 仍由目标负责。 |
| RV64 Frame standalone fallback | 生产路径传入完整 ABI plan；standalone 后备有独立消费者，不是两套生产实现。 |
| `continue-on-error`、SQLite status 收集 | 集中 owner outcome 最终裁决，不能视为静默吞错。 |
| AST clone 共用 FunctionBody | Inline clone 按 FunctionId 追加但合法共享源 `body_block` 和 local range；Body ownership 在 clone 前校验。MC-006 是调用实例事实被写进共享 CALL，而非共享树本身有错。 |
| ASM operand binding 双数组 | Backend contract 按序显式设置 `binding.operand_index=i`，Core 发布前检查索引一致；这是生产者/消费者协议。 |
| Core lowering 对失败输出的保护 | `minic_core_lower_function_with_index` 用暂态 lowered 构造并验证，成功才 move 到 output；不能把过程中的 append 当成污染原 Core 对象。 |

## 审计与 CI 口径

- `src/` 当前跟踪 **332 个 C/H 文件**。`src/compiler` 11/11 已全面阅读；Frontend/Core/Target 已全面盘点并深查关键调用链，但仍有未逐行深读文件。Target 账本共 206 项，上轮结果 `Full=7 / Targeted=4 / Pending=195`，`drift=125` 表示旧审查随源码变更失效；账本 `PASS` **不等于代码全审完**。
- GNU Linux 三 ISA × 七路是冻结预处理 `.i` 经 MiniC `-S` 的**发现型编译成功率**；工作流成功退出不等于 `compiled=total`，要看每 shard 的报告，且 campaign 通常需要显式触发。不等同 GNU AS、ELF、QEMU。
- 四架构 runtime workflow **有真实交叉汇编/链接/QEMU 步骤**；不能因某个 source-only native smoke 的假 QEMU 标题就否定这些独立的真实运行证据。
- 任一修复需要精确 HEAD 的目标回归。对运行风险至少区分 `MiniC -S`、GNU AS、ELF/重定位、QEMU/原生执行四种证据层。

## 新条目模板

```markdown
### MC-<NEXT-ID> 一句话描述（按现有最大编号递增）

- 优先级：P0/P1/P2/P3；状态：INVESTIGATING；证据：SUSPICION。
- 最近核对：<commit>；涉及架构：<targets>。
- 位置：`path:line`（必要时附函数名，避免行号漂移）。
- 现象与最小反例：<输入、命令、期望 vs 实际>。
- 生产者/持有者/消费者：<完整路径>。
- 已排除的正常设计：<为什么不是 ABI/模式差异>。
- 下一验证：<GCC/ASan/ELF/QEMU；仍缺什么>。
- 修复/关闭条件：<所需测试、精确 HEAD 记录、commit/PR>。
```

**维护原则：** 缺陷可以先挂起；证据不能凭印象升级；合理设计要明确记为已排除。真正进入修复批次时，把同一根因的条目联动关闭，并在这里记下提交与精确验证结果。
