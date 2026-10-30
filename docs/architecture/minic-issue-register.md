# MiniC 缺陷、风险与架构债台账

> **长期维护文档，不是立即修复清单。** 发现问题先记录、核实、分类；是否进入某一批重构另行决定。
>
> 首次建档：2026-10-30；最近源码复核：`6b9e52e27`（`refactor/minic-target-language-boundary`）。
> 范围：MiniC 的 Frontend、Compiler、Core、Target、Driver 和验证流程；不包含 MiniAS、MiniPP、MiniLD、NaSeed。

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

## 审计与 CI 口径

- `src/` 当前跟踪 **332 个 C/H 文件**。`src/compiler` 11/11 已全面阅读；Frontend/Core/Target 已全面盘点并深查关键调用链，但仍有未逐行深读文件。Target 账本共 206 项，上轮结果 `Full=7 / Targeted=4 / Pending=195`，`drift=125` 表示旧审查随源码变更失效；账本 `PASS` **不等于代码全审完**。
- GNU Linux 三 ISA × 七路是冻结预处理 `.i` 经 MiniC `-S` 的**发现型编译成功率**；工作流成功退出不等于 `compiled=total`，要看每 shard 的报告，且 campaign 通常需要显式触发。不等同 GNU AS、ELF、QEMU。
- 四架构 runtime workflow **有真实交叉汇编/链接/QEMU 步骤**；不能因某个 source-only native smoke 的假 QEMU 标题就否定这些独立的真实运行证据。
- 任一修复需要精确 HEAD 的目标回归。对运行风险至少区分 `MiniC -S`、GNU AS、ELF/重定位、QEMU/原生执行四种证据层。

## 新条目模板

```markdown
### MC-023 一句话描述

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
