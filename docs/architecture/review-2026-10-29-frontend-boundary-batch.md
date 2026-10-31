# MiniC Frontend 第三轮跨层结构清单与批量改造（2026-10-29）

基线：`a085dd061`，在此前已记录的 84/84 MiniC `frontend/compiler/CLI/API` 文件完整审读之上，复核自首轮基线 `3866af2f64` 以来这 84 个文件的 16 个已修改文件及跨模块调用关系。第三轮重点是 `TargetInfo` 的 C 语义/ABI 准入、file-scope 声明事务、GNU function-pointer ABI 属性、作用域权威表。`src/target/{target_info,data_layout,target_catalog}.c/.h` 仅审计不修改；Core、backend、其它架构文件不在本分片修改。任何未重新逐字复核的新生产代码，不另称本轮 full。

## 优先级清单与处理界限

| 编号 | 优先级 | 结构性问题与精确位置 | 本分片处理状态 |
| --- | --- | --- | --- |
| B3-01 | **P1** | 同一 GNU function-pointer `ms_abi` / informational attribute 语义在 `parser_record.c:parse_function_pointer_field_declarator/consume_record_field_attribute/apply_record_field_declaration_attributes`、`parser_typedef.c:parse_parenthesized_function_typedef` 和 `parser_function_parameters.c:parse_function_pointer_parameter_declarator` 独立复制四次。旧 record 与 typedef 路径检查 `has_arguments`，parameter 路径未检查；均直接调用 TargetInfo、按语义类过滤。 | **已收敛**到 `parser_attribute.c:minic_parser_consume_function_pointer_attribute` 和列表入口 `minic_parser_apply_function_pointer_declarator_attributes`；`parser_internal.h` 仅暴露两个 private API。record declaration-head 的 `context` 完整初始化并将转换后的 `field_type` 回写；TargetInfo 仍是唯一 ABI 准入者。 |
| B3-02 | **P1，已复现原错误** | `parser_global.c:parse_preformed_static_array_definition` 遇 `typedef int A[3]; static A x; static A x={1,2,3};` 直接调用 `minic_c0_program_add_global_object`，不复用既存 tentative 对象；GCC 接受、MiniC 原报 `cannot begin pre-formed static array definition`。 | **已修**：复用 `minic_parser_global_begin_definition`，array composite 使用 `declaration_sema.c` 既有合并规则；`parse_static_scalar_array` 的有初始化路径也复用该入口。`global_merge_static_tentative` 成为 scalar array、普通零定义和逗号 declarator list 的共同 tentative owner，旧互相复制的状态检查删除。原 `[N]`/`[]`/`typedef` 类型 identity 保持在对象的 canonical descriptor；`static const T[]` read-only 由递归 element qualifier 判断。 |
| B3-03 | **P1/P2 设计债，未改** | `type.h:MinicTypeBaseKind` 以 `MINIC_TYPE_BASE_AARCH64_*` 和 `MINIC_TYPE_BASE_X86_VA_LIST_CURSOR` 维护专属语言类型；`ast_verifier.c:~90-130` 再手写每种 type 的 admissibility。`MINIC_CALLING_CONVENTION_MS_X64` 亦为具体 ABI 名，但它是 type identity 而非代码生成策略，不能删。 | **保持 OPEN**。下文给低风险渐进迁移契约，不凭代码行数删除已被 glibc/EFI 头使用的真实类型。 |
| B3-04 | P2，由 root 持有 | `tools/minic-cc/main.c` 的 `-march/-mabi` 仍查询 `isa_family` 后直接调用 RISC-V resolver；`target_info.c` 同一 descriptor 还有 PIC、variadic ABI 和 `riscv_isa` invocation 状态。 | Root 单独改 option/capability boundary，本分片不碰 CLI/target 公共文件。 |
| B3-05 | P2，已收敛旧债 | 旧 record tag、enum tag 分离的 `parser_core.c`/`parser_enum.c` 当前 HEAD 已统一为 `MinicParserTagBinding`，scope frame 保存 `tag_begin`；本轮不再把这部分误报为未合并代码。 | 检查与 local ordinary binding / typedef 缓存的 shadow/rollback 边界仍值得后续故障注入，但不重复改已经统一的 tag namespace。 |
| B3-06 | P1/P2 运行正确性风险，未改 | `parser_expression.c:parse_floating` 对 `long double` 使用 `strtod`→binary128，x86 x87 源级 `1.0000000000000001L` 对照已证明精度损失；`data_layout.h` **已具备** `MinicByteOrder`，不能再声称没有 endian schema，但 long-double **encoding**（x87-80 与 binary128）仍缺独立 trait。 | 保持 OPEN，必须完整协同 lexer/AST constant/Core/helper/emitter 设计，不做孤立替换。 |
| B3-07 | P2 残留声明重构 | `parser_external_object.c:record_external_tentative_object`、`parser_global.c` internal-linkage tentative/definition、`declaration_sema.c:minic_declaration_merge_external_object` 仍分别组织语言不同 linkage 的提交顺序；还有 inferred record array 和外部声明列的特殊 grammar。 | 本批只合并内部 static 重复状态，不强行合并不同 linkage/储存期；下一批先做 2×2（extern/static、tentative/definition）矩阵与 OOM 注入再决定公共 StateTransition API。 |

## B3-01 统一后的契约

1. GNU attribute 词法解析、目标对象合法性与解析失败的精确位置由 `parser_attribute.c` 统一负责。
2. `ms_abi` 只有无参数、目标允许 `MINIC_CALLING_CONVENTION_MS_X64` **function-pointer declaration**、并成功产生带 convention identity 的 interned function type 时才算消费；这并不自动承诺任何非 x86 目标能生成对应机器 CALL。
3. Informational/diagnostic/optimization/control-flow 且允许 FUNCTION 的 attribute 在当前声明上下文可作为 parse-only；layout/symbol/未知 ABI policy 不得静默吞掉。record field 同时可保留自身 `packed/aligned` 语义。
4. 统一入口接收已解析 `MinicParsedAttributeList`，列表调用方仅指定原上下文错误消息；单个 attribute callback 也复用同一 predicate，消除“parameter 少验 `has_arguments`”的差异。

## B3-02 统一后的契约

1. 所有 file-scope internal-linkage scalar/array `static` tentative 定义经过唯一 `global_merge_static_tentative`，按 `declaration_sema` 的组合类型规则合并**同一实体**；`static A x; static A x=...;` 不再另建 object。
2. 定义入口统一用 `minic_parser_global_begin_definition`，成功后使用 Program 中对象的 canonical descriptor 写初始化器，避免 typed typedef 与实际 global 对象类型出入。
3. 旧临时 array type 若为尾部 transient 且没有 AST owner，现有 `discard_last_array_type` 规约仍执行；不把 incomplete typedef 的共享 descriptor 意外补成某一个对象的 bound。
4. 冲突长度和重复 full definition 保持拒绝；GCC 对照包含 fixed typedef、char literal、pointer pair、const array、两个使用同一 incomplete typedef 但推断为不同长度的对象。

## B3-03 Target type identity 渐进迁移建议（不改生产）

现有 `MinicType` 和 `MinicTypeBaseKind` 数值既服务类型兼容，又是 AST verifier/Core/backend 的契约；机械把 `AARCH64_*` 改名或给所有 target 填一个 “opaque” 会隐瞒 ABI/layout 限制。分三阶段：

1. 先定义 target-owned `MinicTargetExtendedTypeDescriptor`：**stable type identity**、C spelling/等价关系、仅声明合法性、storage layout 许可、call/return ABI 许可、运算 lowering 许可分别标记。当前 `AARCH64_*`/x86 va cursor/TS18661 作为 compatibility adapter 映射，保留 serialized kind 值不变。
2. 让 `parser_type.c` 和 `ast_verifier.c` 共同走一项 `TargetTypeTraits` 校验（含 x86 va cursor 必须 pointer-depth≥1 等不变量），禁止 parser 支持 header-only typedef 就推断可以 `sizeof`、传参、执行算术。`type.c` 的通用构造/比较只关心 stable identity 与 qualification。
3. 扩到新目标时，仅登记目标扩展 trait + backend 专属 ABI/机器动作，不在前端增长 `MINIC_TYPE_BASE_ARCH_*` 或第三套 target switch；以 Linux 原版预处理头、五目标 `-S`、GCC ABI 和负例分别验收 language-only 与 executable 分界。

## GCC / MiniC 差分验收

- `tests/compiler/c0/probe-function-pointer-attribute-policy.sh`：GCC 原生运行 2、MiniC x86 原生运行 2；便携 parse-only typedef/record/parameter 五目标 `-S`；MS ABI 仅 x86 可运行、其余四目标拒绝；`ms_abi(1)` 在 typedef/record/parameter 三处由 GCC 与 MiniC 全部拒绝。
- `tests/compiler/c0/probe-static-typedef-array-redeclaration.sh`：GCC 原生运行 1、MiniC x86 原生运行 1、五目标 `-S` 5/5；冲突长度和重复定义两种 GCC/MiniC 五目标负例。
- 沿用上一批 `probe-static-array-redeclaration.sh`（scalar integer/char/pointer）及 `run-gnu-efi-msabi-attributed-pointer-declarators.sh`、`probe-x86-msabi-record-abi.sh`。以上 fixture 不是 Linux 整个七路编译通过证明，只是受影响语言契约的差分。

所有 production 修改仅位于 `src/frontend`，未改 Core/backend/target/CLI；未提交和推送。`long double` 源级精度 bug 仍 OPEN。
