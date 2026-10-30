# MiniC 缺陷、风险与架构债台账

> **长期维护文档，不是立即修复清单。** 发现问题先记录、核实、分类；是否进入某一批重构另行决定。
>
> 首次建档：2026-10-30；最近源码复核：`f4045d8f9`（`refactor/minic-target-language-boundary`）。
> 范围：MiniC 的 Frontend、Compiler、Core、Target、Driver 和验证流程；不包含 MiniAS、MiniPP、MiniLD、NaSeed。

配套阅读：[全局架构与所有权图谱](minic-global-contract-map.md) · [函数契约审计台账](minic-function-contract-register.md)。

## 使用约定

- **编号永久不复用。** 新发现分配新的 `MC-xxx`；同一个根因涉及多个可独立触发的位置，可以分别登记，并注明共同根因。
- **状态与优先级独立。** `OPEN`（确认待处理）、`INVESTIGATING`（证据不完整）、`DEBT`（已证实的维护债，未必有功能错误）、`FIXED-UNVERIFIED`、`VERIFIED`、`CLOSED-NOT-A-BUG`。`P0` 表示影响可能严重，不代表已经动态复现。
- **证据等级独立。** `ASAN`＝运行时内存错误堆栈；`GCC-DIFF`＝与有效 GCC 输入/生成物对照；`ELF-ORACLE`＝真实目标文件差分；`ASM-ONLY`＝生成汇编及地址/指令关系；`SOURCE-TRACE`＝分配、所有权、调用链证明；`CI-LOG`＝实际工作流/脚本结果；`DESIGN-REVIEW`＝设计与消费者核对；`SUSPICION`＝尚缺反例。
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
| [MC-028](#mc-028-rv64-的-int128-移位计数类型限制) | P2 | INVESTIGATING | SOURCE-TRACE | RV64 128-bit 移位显式拒绝 128-bit RHS，尚待 GNU 差分 |

已验证非问题：**[MC-026](#mc-026-source-form-id-的实际身份边界)**，状态 `CLOSED-NOT-A-BUG`，保留编号和消费者证据，避免重复误报。

## 索引：确实存在但不宜立即改的架构与验证债

| ID | 优先级 | 状态 | 证据 | 内容 |
| --- | --- | --- | --- | --- |
| [MC-019](#mc-019-两套-bool-恒零证明扫描重叠) | P2 | DEBT | DESIGN-REVIEW | 两种保守策略共享了大量全程序扫描 |
| [MC-020](#mc-020-无条件的-genl-调试输出) | P2 | DEBT | SOURCE-TRACE | 六处 Linux 特定诊断无开关输出 |
| [MC-021](#mc-021-历史测试与脚本可达性) | P2 | DEBT | SOURCE-TRACE | 老负例、历史补丁和 inactive 流程需分类 |
| [MC-022](#mc-022-符号特化回归的-clone-身份断言不足) | P1 | DEBT | CI-LOG | 测试有真实语义覆盖，但日志断言不能证明 clone 身份 |

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

- **位置：** `src/frontend/parser_static_initializer.c:3194`、`parser_external_object.c:650`。
- **疑点：** 全局检索 38 处 `MinicParser probe=*parser` 类浅拷贝，35 处属于不修改拥有型表的词法前瞻，MC-004/005 是 1 处完整语义解析触发的两种 UAF；此处另有 2 个 array extent 语义探测需查。
- **证据边界：** 当前快照对 Record/Enum 数量变更通常会拒绝；尚无恢复后的 UAF 反例，不能定为已确认 bug。

### MC-018 AArch64 短分支距离依据基本块数

- **位置：** `src/target/aarch64/block_emit.c:378–404`。
- **疑点：** `block_count<=256` 并不在逻辑上保证 `cbnz` 的 ±1MiB 范围，但当前构造有效超长单块测试受容量/ASM 资格限制，**没有可复现汇编失败**。
- **下一证据：** 找到可接受的大块合法 Core 输入，或证明布局上界与块数真正相关。

### MC-023 静态聚合后向覆写跨递归借用 Record

- **位置：** `src/frontend/parser_static_initializer.c:2360–2524`，`overwrite_static_zero_record_value` / `overwrite_static_zero_record_constant`。
- **疑点：** 当前函数从 `program->records[record_id]` 借用 `MinicRecord*`，在循环内调用能递归解析嵌套表达式的 `overwrite_static_zero_field_value`，之后仍访问 `record->fields` / `is_union`。如果其中一个合法嵌套初始化会通过内联 `struct/union` 定义导致 Program.records 扩容，借用地址可能失效。
- **反证边界：** `parse_static_record_constant`、`parse_static_union_constant` 早已使用 `MinicRecord` 按值快照，设计成立。不能因另两函数有快照，直接断言 backward 路径必然可触发；当前没有 ASan/GCC 可接受的动态探针。
- **下一证据：** 构造 GNU backward designated aggregate 加嵌套类型声明，调节 Record arena 容量，以有效 GCC 输入做五目标 ASan；如递归实际不允许添加 Record，则关闭为非问题。

### MC-024 Common Core 中的 rK 约束特例

- **位置：** `src/core/core_lower_asm_structured.c:204–260` 的 `core_asm_try_immediate_output_csr`，对照 `src/core/core_lower_asm_target.c:10`。
- **疑点：** Common Core 直接识别 Linux RISC-V GNU ASM 的 `rK`、固定 output/input/clobber 形状并优先完成立即量特化。项目已有 Target-owned contract/资格机制，需明确这处 source constraint 语义是否可被目标拒绝、是否只属于受控兼容 fastpath。
- **证据边界：** 未发现跨 ISA 错编实例，Core 也没有直接用 `MINIC_ARCH_*` 枚举发码。需要从 `minic_core_lower_inline_asm` 的 probe 顺序追踪真消费者；**不能按出现 `rK` 就宣告架构泄露 Bug**。
- **下一证据：** 验证五目标 target-required admission、fastpath 输入输出等价与退回路径。若属于安全的旧指令约束兼容，标明正当特例即可。

### MC-025 AArch64 公共全局 object 类型伪操作兼容性

- **位置：** `src/target/aarch64/codegen.c:159` 的目标配置 `"%object"`，及 `src/target/common/global_emit.c:1250` 固定输出 `.type ...,@object` 的 simple-global 路径。
- **疑点：** 两种 object suffix 语法可能不同，需确认哪种路径真正会执行以及 AArch64 GNU AS 对 `@object` 的接受情况。
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

- **位置：** `src/target/riscv64/scalar_emit.c:101–122,263–290`，对照 `src/core/core_lower_arithmetic.c:490–540`、`src/core/core_ir_verify.c:123–136` 与 `src/target/riscv32/rv32_wide_emit.c:32–67`。
- **疑点：** RV64 `__int128` 左移和右移发射器均在 RHS 为 `__int128` 时显式返回 false；Core lowering 保留右操作数整数类型，Core verifier 准许整数右值。RV32 的宽移位消费者另有双字 RHS 处理。因此可能存在合法 C 程序在 RV64 的目标实现缺口。
- **证据边界：** 只有源码路径对照，**尚未证明有效输入实际进入该 Core 形式，也未完成 GCC 同目标编译差分**。不能因为当前两处显式拒绝就断言 Linux workload 已失败。
- **下一证据：** 对 `unsigned __int128 x, n; return x << n;` 及右移、常量/非常量 RHS 构造独立函数，分别用 GNU RV64 编译和 MiniC `-S`，然后 GNU AS/ELF/执行检查。若前端正规化或其他合法路径自动转换 RHS，关闭为非问题；否则按最小反例升级。

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

## 已核对并排除的误判（不要在未有新证据时重新打开）

| 项目 | 结论及当前依据 |
| --- | --- |
| `FunctionId` / `ExpressionId` 对应数组下标 | **有意稳定身份**；append-only ID 可以准确标识对象，不是两个数组碰巧同序。错误是跨实例写共享 CALL，不是使用整数索引。 |
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
