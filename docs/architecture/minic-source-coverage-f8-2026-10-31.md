# MiniC 第八轮编译器源码覆盖清单

> 冻结审计快照：`refactor/minic-target-language-boundary` / `3d504e7ee4a7`，2026-10-31。
> 范围仅 MiniC 编译器。用于证明每个文件有明确清点入口，**不是另一份问题台账**。
> 历史问题及确认状态仍以 [MC 台账](minic-issue-register.md) 为准；当前架构与 A8 线索见 [总索引](README.md)。

## 覆盖口径

- `F1`：当前分支已清点此文件及其路径、模块和主要入口；与 `f6e9eb870d80` 按字节比较。**不等于逐函数 U2**。
- `R2`：特定热点另行追到 producer、consumer、所有权和失败边界，证据写入 [函数契约台账](minic-function-contract-register.md)及 [总索引 A8](README.md#3-本轮新增的待补证据和清理接缝)；不能给整文件无条件盖章。
- `E`：差分测试/ASan/GNU AS/ELF/QEMU 的真实证据。未执行的新测试一律不因这份清单获得 `E`。

统计：`src/` **337** 个 C/H（187 C、150 H）+ 两个 CLI 源文件 = **339**；另核对 `target_registration.def` 与 `Makefile`，共 **341 个构建相关输入**。`Makefile` 的 MiniC 源列表覆盖 187/187 个编译器 `.c` 文件（Makefile 同时列有其他工具链源文件，本检查不把它们算入编译器）。

对两个审计版本的整个 `src/` 做逐文件差异：只有 `x86_64/codegen.c`、`frame.h`、`machine_emit.c` **3 个变化**；其他 334 个 C/H 字节相同。两个 CLI 与 `Makefile`、注册定义不变。该比对只证明源码文本的改变范围，绝不代表运行行为已验证。

| 分组 | 文件数 | 已完成的最低核对 | 本轮不能越过的证据边界 |
| --- | ---: | --- | --- |
| Frontend | 70 | F1；关键接口/旧路径定点 R2 | 重点调用链 R2，非每个语法/ISA 变体 U2 |
| Compiler | 11 | F1；关键接口/旧路径定点 R2 | 生产入口/调用关系已核，单函数例外仍须证据 |
| Core | 48 | F1；关键接口/旧路径定点 R2 | 生产入口/调用关系已核，单函数例外仍须证据 |
| Target 根 | 12 | F1；关键接口/旧路径定点 R2 | 生产入口/调用关系已核，单函数例外仍须证据 |
| Target Common | 55 | F1；关键接口/旧路径定点 R2 | 55 个公共文件接口已建索引；部分深层 fallback 尚需逐函数验证 |
| Target Source Policy | 6 | F1；关键接口/旧路径定点 R2 | 生产入口/调用关系已核，单函数例外仍须证据 |
| RISC-V Common | 13 | F1；关键接口/旧路径定点 R2 | 生产入口/调用关系已核，单函数例外仍须证据 |
| RV32 | 16 | F1；关键接口/旧路径定点 R2 | 重点调用链 R2，非每个语法/ISA 变体 U2 |
| RV64 | 42 | F1；关键接口/旧路径定点 R2 | 重点调用链 R2，非每个语法/ISA 变体 U2 |
| x86-64 | 20 | F1；关键接口/旧路径定点 R2 | 3 个改动文件需单独 R2；巨型 ASM 规则仍有未逐变体验算部分 |
| AArch64 | 22 | F1；关键接口/旧路径定点 R2 | 重点调用链 R2，非每个语法/ISA 变体 U2 |
| LoongArch64 | 22 | F1；关键接口/旧路径定点 R2 | 重点调用链 R2，非每个语法/ISA 变体 U2 |

## 全部 337 个源码文件（按责任目录）

### Frontend（70）

- `src/frontend/ast.c`
- `src/frontend/ast.h`
- `src/frontend/ast_function.c`
- `src/frontend/ast_global.c`
- `src/frontend/ast_traversal.c`
- `src/frontend/ast_traversal.h`
- `src/frontend/ast_verifier.c`
- `src/frontend/ast_verifier.h`
- `src/frontend/attribute.c`
- `src/frontend/attribute.h`
- `src/frontend/cast_normalization.c`
- `src/frontend/cast_normalization.h`
- `src/frontend/const_eval.c`
- `src/frontend/const_eval.h`
- `src/frontend/declaration_sema.c`
- `src/frontend/declaration_sema.h`
- `src/frontend/expression_semantics.c`
- `src/frontend/expression_semantics.h`
- `src/frontend/function_body.c`
- `src/frontend/function_body.h`
- `src/frontend/initializer.c`
- `src/frontend/initializer.h`
- `src/frontend/lexer.c`
- `src/frontend/lexer.h`
- `src/frontend/parser.h`
- `src/frontend/parser_attribute.c`
- `src/frontend/parser_builtin_memory.c`
- `src/frontend/parser_builtin_memory.h`
- `src/frontend/parser_builtins.c`
- `src/frontend/parser_constant.c`
- `src/frontend/parser_core.c`
- `src/frontend/parser_declarator.c`
- `src/frontend/parser_enum.c`
- `src/frontend/parser_expression.c`
- `src/frontend/parser_external_object.c`
- `src/frontend/parser_external_object.h`
- `src/frontend/parser_function.c`
- `src/frontend/parser_function_parameters.c`
- `src/frontend/parser_global.c`
- `src/frontend/parser_global_lookup.c`
- `src/frontend/parser_gnu_inline_asm.c`
- `src/frontend/parser_gnu_inline_asm.h`
- `src/frontend/parser_internal.h`
- `src/frontend/parser_labels.c`
- `src/frontend/parser_labels.h`
- `src/frontend/parser_local_declarations.c`
- `src/frontend/parser_local_declarations.h`
- `src/frontend/parser_member.c`
- `src/frontend/parser_postfix.c`
- `src/frontend/parser_pragma.c`
- `src/frontend/parser_record.c`
- `src/frontend/parser_runtime_initializer.c`
- `src/frontend/parser_runtime_initializer.h`
- `src/frontend/parser_statement.c`
- `src/frontend/parser_statement_control.c`
- `src/frontend/parser_statement_control.h`
- `src/frontend/parser_static_assert.c`
- `src/frontend/parser_static_constants.c`
- `src/frontend/parser_static_constants.h`
- `src/frontend/parser_static_initializer.c`
- `src/frontend/parser_static_initializer.h`
- `src/frontend/parser_string.c`
- `src/frontend/parser_type.c`
- `src/frontend/parser_type_query.c`
- `src/frontend/parser_typedef.c`
- `src/frontend/semantic_snapshot.h`
- `src/frontend/token.c`
- `src/frontend/token.h`
- `src/frontend/type.c`
- `src/frontend/type.h`

### Compiler（11）

- `src/compiler/compiler.c`
- `src/compiler/core_function_set.c`
- `src/compiler/core_function_set.h`
- `src/compiler/core_reachability.c`
- `src/compiler/core_reachability.h`
- `src/compiler/inline_boolean.c`
- `src/compiler/inline_integer.c`
- `src/compiler/inline_reference.c`
- `src/compiler/inline_specialize.h`
- `src/compiler/inline_specialize_internal.h`
- `src/compiler/inline_symbolic.c`

### Core（48）

- `src/core/address_alignment.h`
- `src/core/cfg_constant_calls.c`
- `src/core/cfg_constant_calls.h`
- `src/core/core_const_global_record.c`
- `src/core/core_const_global_record.h`
- `src/core/core_ir.c`
- `src/core/core_ir.h`
- `src/core/core_ir_dump.c`
- `src/core/core_ir_internal.h`
- `src/core/core_ir_verify.c`
- `src/core/core_lower.c`
- `src/core/core_lower.h`
- `src/core/core_lower_address.c`
- `src/core/core_lower_arithmetic.c`
- `src/core/core_lower_asm.c`
- `src/core/core_lower_asm_internal.h`
- `src/core/core_lower_asm_operands.c`
- `src/core/core_lower_asm_scalar_forms.c`
- `src/core/core_lower_asm_structured.c`
- `src/core/core_lower_asm_target.c`
- `src/core/core_lower_assignments.c`
- `src/core/core_lower_bit_fields.c`
- `src/core/core_lower_bool.c`
- `src/core/core_lower_calls.c`
- `src/core/core_lower_const_facts.c`
- `src/core/core_lower_expr_flow.c`
- `src/core/core_lower_expr_scalar.c`
- `src/core/core_lower_expression.c`
- `src/core/core_lower_if.c`
- `src/core/core_lower_internal.h`
- `src/core/core_lower_local_effects.c`
- `src/core/core_lower_loop.c`
- `src/core/core_lower_metadata.c`
- `src/core/core_lower_record.c`
- `src/core/core_lower_scalar_values.c`
- `src/core/core_lower_switch.c`
- `src/core/core_lower_types.c`
- `src/core/core_lower_varargs.c`
- `src/core/linear_vararg_cursor.h`
- `src/core/local_facts.c`
- `src/core/local_facts.h`
- `src/core/opt_pure_statement.h`
- `src/core/reentry_index.c`
- `src/core/reentry_index.h`
- `src/core/source_loop_shapes.c`
- `src/core/source_loop_shapes.h`
- `src/core/value_uses.c`
- `src/core/value_uses.h`

### Target 根（12）

- `src/target/backend.c`
- `src/target/backend.h`
- `src/target/backend_core_usage.c`
- `src/target/backend_core_usage.h`
- `src/target/data_layout.c`
- `src/target/data_layout.h`
- `src/target/target_catalog.c`
- `src/target/target_catalog.h`
- `src/target/target_info.c`
- `src/target/target_info.h`
- `src/target/target_machine_options.c`
- `src/target/target_machine_options.h`

### Target Common（55）

- `src/target/common/aggregate_shape.c`
- `src/target/common/aggregate_shape.h`
- `src/target/common/core_block_walk.h`
- `src/target/common/core_call_index.c`
- `src/target/common/core_call_view.c`
- `src/target/common/core_call_view.h`
- `src/target/common/core_frame_layout.h`
- `src/target/common/core_function_facts.c`
- `src/target/common/core_function_facts.h`
- `src/target/common/core_function_plan.c`
- `src/target/common/core_function_plan.h`
- `src/target/common/core_instruction_family.h`
- `src/target/common/core_numeric_shape.c`
- `src/target/common/core_numeric_shape.h`
- `src/target/common/core_object_intervals.c`
- `src/target/common/core_object_intervals.h`
- `src/target/common/core_object_reuse.c`
- `src/target/common/core_object_reuse.h`
- `src/target/common/core_object_storage.c`
- `src/target/common/core_object_storage.h`
- `src/target/common/core_spill_layout.c`
- `src/target/common/core_spill_layout.h`
- `src/target/common/core_value_slots.c`
- `src/target/common/core_value_slots.h`
- `src/target/common/direct_block_context.h`
- `src/target/common/direct_block_register.h`
- `src/target/common/direct_call_plan.c`
- `src/target/common/direct_call_plan.h`
- `src/target/common/direct_function_emit.h`
- `src/target/common/direct_function_plan.c`
- `src/target/common/direct_function_plan.h`
- `src/target/common/direct_function_profile.c`
- `src/target/common/direct_function_profile.h`
- `src/target/common/direct_instruction_semantics.c`
- `src/target/common/direct_instruction_semantics.h`
- `src/target/common/direct_linux_bug_asm.h`
- `src/target/common/direct_lp64_abi.h`
- `src/target/common/direct_machine_dispatch.h`
- `src/target/common/direct_preflight.c`
- `src/target/common/direct_preflight.h`
- `src/target/common/direct_program_emit.c`
- `src/target/common/direct_program_emit.h`
- `src/target/common/direct_residual_plan.c`
- `src/target/common/direct_residual_plan.h`
- `src/target/common/elf_alias_emit.h`
- `src/target/common/global_emit.c`
- `src/target/common/global_emit.h`
- `src/target/common/lp64_aggregate.c`
- `src/target/common/lp64_aggregate.h`
- `src/target/common/lp64_frame.c`
- `src/target/common/lp64_frame.h`
- `src/target/common/lp64_scalar.c`
- `src/target/common/lp64_scalar.h`
- `src/target/common/program_emit.h`
- `src/target/common/program_visit.h`

### Target Source Policy（6）

- `src/target/source_policy/aarch64.c`
- `src/target/source_policy/loongarch64.c`
- `src/target/source_policy/riscv32.c`
- `src/target/source_policy/riscv64.c`
- `src/target/source_policy/source_policy.h`
- `src/target/source_policy/x86_64.c`

### RISC-V Common（13）

- `src/target/riscv/frame_address_policy.h`
- `src/target/riscv/function_symbol.h`
- `src/target/riscv/gpr_abi_cursor.h`
- `src/target/riscv/instruction_select.h`
- `src/target/riscv/isa_features.h`
- `src/target/riscv/isa_resolve.c`
- `src/target/riscv/isa_resolve.h`
- `src/target/riscv/record_copy_bytes.c`
- `src/target/riscv/record_copy_bytes.h`
- `src/target/riscv/register_identity.h`
- `src/target/riscv/typed_memory.h`
- `src/target/riscv/word_operations.c`
- `src/target/riscv/word_operations.h`

### RV32（16）

- `src/target/riscv32/rv32_abi_cursor.c`
- `src/target/riscv32/rv32_abi_cursor.h`
- `src/target/riscv32/rv32_abi_plan.c`
- `src/target/riscv32/rv32_abi_plan.h`
- `src/target/riscv32/rv32_call.c`
- `src/target/riscv32/rv32_emit.c`
- `src/target/riscv32/rv32_emit.h`
- `src/target/riscv32/rv32_fp_emit.c`
- `src/target/riscv32/rv32_frame.c`
- `src/target/riscv32/rv32_global_emit.c`
- `src/target/riscv32/rv32_global_emit.h`
- `src/target/riscv32/rv32_integer_emit.c`
- `src/target/riscv32/rv32_internal.h`
- `src/target/riscv32/rv32_memory_emit.c`
- `src/target/riscv32/rv32_scalar.c`
- `src/target/riscv32/rv32_wide_emit.c`

### RV64（42）

- `src/target/riscv64/abi.c`
- `src/target/riscv64/abi.h`
- `src/target/riscv64/asm_plan.c`
- `src/target/riscv64/asm_plan.h`
- `src/target/riscv64/call_emit.c`
- `src/target/riscv64/call_emit.h`
- `src/target/riscv64/codegen.h`
- `src/target/riscv64/codegen_function.c`
- `src/target/riscv64/codegen_internal.h`
- `src/target/riscv64/codegen_support.c`
- `src/target/riscv64/core_admission.c`
- `src/target/riscv64/core_admission.h`
- `src/target/riscv64/core_codegen.c`
- `src/target/riscv64/core_codegen.h`
- `src/target/riscv64/fp_emit.c`
- `src/target/riscv64/fp_emit.h`
- `src/target/riscv64/frame_access.c`
- `src/target/riscv64/frame_access.h`
- `src/target/riscv64/frame_layout.c`
- `src/target/riscv64/frame_layout.h`
- `src/target/riscv64/frame_requirements.c`
- `src/target/riscv64/frame_requirements.h`
- `src/target/riscv64/function_abi_plan.c`
- `src/target/riscv64/function_abi_plan.h`
- `src/target/riscv64/global_emit.c`
- `src/target/riscv64/global_emit.h`
- `src/target/riscv64/inline_asm_emit.c`
- `src/target/riscv64/inline_asm_emit.h`
- `src/target/riscv64/isa.c`
- `src/target/riscv64/isa.h`
- `src/target/riscv64/legalize.c`
- `src/target/riscv64/legalize.h`
- `src/target/riscv64/memory_emit.c`
- `src/target/riscv64/memory_emit.h`
- `src/target/riscv64/overflow_emit.c`
- `src/target/riscv64/overflow_emit.h`
- `src/target/riscv64/record_snapshot.c`
- `src/target/riscv64/record_snapshot.h`
- `src/target/riscv64/runtime_libcall.c`
- `src/target/riscv64/runtime_libcall.h`
- `src/target/riscv64/scalar_emit.c`
- `src/target/riscv64/scalar_emit.h`

### x86-64（20）

- `src/target/x86_64/abi.c`
- `src/target/x86_64/abi.h`
- `src/target/x86_64/asm_contract.c`
- `src/target/x86_64/asm_contract.h`
- `src/target/x86_64/call_plan.c`
- `src/target/x86_64/call_plan.h`
- `src/target/x86_64/codegen.c`
- `src/target/x86_64/codegen.h`
- `src/target/x86_64/codegen_options.h`
- `src/target/x86_64/frame.c`
- `src/target/x86_64/frame.h`
- `src/target/x86_64/global_emit.c`
- `src/target/x86_64/global_emit.h`
- `src/target/x86_64/machine_asm.c`
- `src/target/x86_64/machine_call.c`
- `src/target/x86_64/machine_emit.c`
- `src/target/x86_64/machine_emit.h`
- `src/target/x86_64/machine_fp.c`
- `src/target/x86_64/machine_internal.h`
- `src/target/x86_64/machine_support.c`

### AArch64（22）

- `src/target/aarch64/abi.c`
- `src/target/aarch64/abi.h`
- `src/target/aarch64/asm_contract.c`
- `src/target/aarch64/asm_contract.h`
- `src/target/aarch64/block_emit.c`
- `src/target/aarch64/block_emit.h`
- `src/target/aarch64/call_emit.c`
- `src/target/aarch64/call_emit.h`
- `src/target/aarch64/codegen.c`
- `src/target/aarch64/codegen.h`
- `src/target/aarch64/fp_emit.c`
- `src/target/aarch64/fp_emit.h`
- `src/target/aarch64/frame_emit.c`
- `src/target/aarch64/frame_emit.h`
- `src/target/aarch64/integer_emit.c`
- `src/target/aarch64/integer_emit.h`
- `src/target/aarch64/memory_emit.c`
- `src/target/aarch64/memory_emit.h`
- `src/target/aarch64/residual_emit.c`
- `src/target/aarch64/residual_emit.h`
- `src/target/aarch64/scalar_normalize.c`
- `src/target/aarch64/scalar_normalize.h`

### LoongArch64（22）

- `src/target/loongarch64/asm_contract.c`
- `src/target/loongarch64/asm_contract.h`
- `src/target/loongarch64/block_emit.c`
- `src/target/loongarch64/block_emit.h`
- `src/target/loongarch64/call_emit.c`
- `src/target/loongarch64/call_emit.h`
- `src/target/loongarch64/codegen.c`
- `src/target/loongarch64/codegen.h`
- `src/target/loongarch64/fp_emit.c`
- `src/target/loongarch64/fp_emit.h`
- `src/target/loongarch64/frame_emit.c`
- `src/target/loongarch64/frame_emit.h`
- `src/target/loongarch64/integer_emit.c`
- `src/target/loongarch64/integer_emit.h`
- `src/target/loongarch64/memory_emit.c`
- `src/target/loongarch64/memory_emit.h`
- `src/target/loongarch64/residual_emit.c`
- `src/target/loongarch64/residual_emit.h`
- `src/target/loongarch64/scalar_normalize.c`
- `src/target/loongarch64/scalar_normalize.h`
- `src/target/loongarch64/simd_asm.c`
- `src/target/loongarch64/simd_asm.h`

## 另外四个编译入口/构建事实文件

- `tools/minic/main.c` — 产品 driver，目标 stage 受注册目录限制；见编译器入口契约。
- `tools/minic-cc/main.c` — 单 TU `.i→.s` 编译器 CLI；创建本次 invocation Target。
- `src/target/target_registration.def` — 同一 X-macro 注册信息供 catalog、TargetInfo 和 Backend 使用。
- `Makefile` — MiniC 编译单元的构建清单；已交叉核对 187/187 `.c`。

## 盲区与下一轮动作

1. 所有路径均有文件级覆盖；但 **F1/R1 文件级 ≠ 所有内部函数的 R2/U2**。当前深证集中在 Parser 生命周期、数组/静态声明、Core CFG/facts、CALL 特化、Common Global、ABI/ASM、x86 head 等生产热点。
2. 超大 GNU ASM policy 表不能靠一次读目录算出 100% 变体正确性；每组必须有源资格、Core form、机器 emitter、GNU 正反例及 ELF 证据。
3. 1854 个 `tests/{compiler,core,target}` 文件不在本次 341 个编译器实现输入中；按相关 MC 和 A8 执行最小验证时逐个引用，不冒称测试全量覆盖。
4. 每次新提交或新目标加入，先比较本清单与 `rg --files src -g '*.c' -g '*.h'`，补新文件责任与审读状态；**旧发现不得因列表重排被删除**。
