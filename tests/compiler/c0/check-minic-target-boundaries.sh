#!/usr/bin/env bash
# Architectural invariants for the already-implemented MiniC backends.
# This guards regressions to ownership, not formatting or line-count targets.
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

fail() { printf 'MINIC_BACKEND_BOUNDARY_FAIL %s\n' "$*" >&2; exit 1; }
exists() { [[ -f "$1" ]] || fail "missing file $1"; }
has() { grep -Fq -- "$2" "$1" || fail "$1 must contain $2"; }
lacks() {
  if grep -Fq -- "$2" "$1"; then
    fail "$1 must not contain $2"
  fi
}

for file in \
  src/target/common/direct_instruction_semantics.c \
  src/target/common/direct_residual_plan.c \
  src/target/common/direct_function_plan.c \
  src/target/x86_64/call_plan.c \
  src/target/riscv64/core_admission.c \
  src/target/riscv64/function_abi_plan.c; do
  exists "$file"
done

has src/target/loongarch64/asm_contract.c 'loong_pte_form('
has src/target/loongarch64/asm_contract.c 'loong_pte_emit('
lacks src/core/core_lower_asm_target.c 'll.d'
has src/target/common/direct_instruction_semantics.c \
  'minic_multi_direct_machine_instruction_shape('
has src/target/common/direct_residual_plan.c \
  'minic_multi_direct_integer_cast_plan('
has src/target/x86_64/machine_call.c \
  'minic_x86_64_plan_outgoing_arguments('
has src/target/riscv64/core_codegen.c \
  'minic_riscv64_core_function_admit_profile('
# RV32 fixed-arity psABI placement must follow the declared parameter, not
# the expression's potentially narrower actual type (RV64/x86 already do).
has src/target/riscv32/rv32_abi_plan.c 'type = i < view.parameter_count ? view.parameter_types[i] : actual;'
has src/target/riscv32/rv32_abi_plan.c 'call_fixed_type'
has src/target/riscv32/rv32_frame.c 'off == SIZE_MAX'
has src/target/riscv32/rv32_frame.c 'strcmp(reg, "t6") == 0'

has src/target/riscv64/frame_layout.c \
  'minic_core_object_reuse_plan_build('
# One Common interval index covers small and large Core functions.
# Never resurrect per-object repeated scans on optional allocation failure.
exists src/target/common/core_object_intervals.c
has src/target/common/core_object_reuse.c \
  'minic_core_object_intervals_index(function, &plan->analysis);'
lacks src/target/common/core_object_reuse.c 'function->object_count >= 8U'
lacks src/target/common/core_object_intervals.c 'CoreTargetValueUseCount'
lacks src/target/common/core_object_intervals.c 'MINIC_TEST_FORCE_CORE_OBJECT_INTERVAL_FALLBACK'
lacks src/target/common/core_object_intervals.c 'Allocation or malformed-index fallback'
has src/target/common/core_spill_layout.c \
  'cached_slot_sizes[value_id] = *slot_size;'

# Real GNU ARM64 sysreg output is u64 (unsigned long long); both source
# admission and actual machine writeback must share ONE target-owned type gate.
has src/target/aarch64/asm_contract.c 'aarch64_linux_sysreg_u64_type('
has src/target/aarch64/asm_contract.c '!aarch64_linux_sysreg_u64_type(pointee)'
lacks src/core/core_lower_asm_target.c 'mrs x9, daif'

# Original LoongArch64 Linux array_index_mask_nospec is exact target-owned
# SLTU + SUB.D with output/register contract, not a cloned Core algorithm.
has src/target/loongarch64/asm_contract.c 'loong_mask_nospec_form('
has src/target/loongarch64/asm_contract.c 'loong_mask_nospec_source('
has src/target/loongarch64/asm_contract.c 'loong_mask_nospec_emit('
lacks src/core/core_lower_asm_target.c 'sltu'

# All targets reuse one ELF custom section and binding owner.
has src/target/common/global_emit.c 'minic_multi_emit_elf_global_section('
has src/target/common/global_emit.c 'minic_multi_emit_elf_global_binding('
has src/target/common/global_emit.c 'minic_multi_elf_safe_section_name('

# Linux local_t plain INC/DEC uses the same x86 target RMW descriptor as SMP.
has src/target/x86_64/asm_contract.c '{"incq",8U,false,true}'
has src/target/x86_64/asm_contract.h 'MINIC_X86_ATOMIC_RMW_LOCAL_DEC64'
lacks src/core/core_lower_asm_target.c 'local_inc'

# One shared Core frame-level-0 verifier, two target-owned SP machine forms;
# level >0 and RISC-V/x86 frame ABI remain unchanged.
has src/target/aarch64/residual_emit.c 'MINIC_CORE_CALL_FRAME_ADDRESS_FRAME'
has src/target/loongarch64/residual_emit.c 'MINIC_CORE_CALL_FRAME_ADDRESS_FRAME'

# Linux local_add_return uses ONE x86 target ISA XADD contract and existing
# Core REGISTER_READWRITE+MEMORY_READWRITE effects, never a new Core opcode.
has src/target/x86_64/asm_contract.c 'x86_linux_local_xadd_source('
has src/target/x86_64/machine_asm.c 'emit_linux_local_xadd('
lacks src/core/core_lower_asm_target.c 'local_add_return'

# Four ISA families, five target profiles: one storage-shape owner, distinct
# physical ABIs. RV32 must never inherit the 8-byte LP64 word policy.
exists src/target/common/aggregate_shape.h
exists src/target/common/aggregate_shape.c
has src/target/common/aggregate_shape.c 'bool minic_common_aggregate_storage_shape('
has src/target/common/aggregate_shape.c 'bool minic_common_aggregate_integral_tree('
for consumer in src/target/x86_64/abi.c \
                src/target/riscv32/rv32_abi_plan.c \
                src/target/riscv64/abi.c \
                src/target/common/lp64_aggregate.c; do
  has "$consumer" 'minic_common_aggregate_storage_shape('
done
has src/target/riscv64/abi.c 'minic_common_aggregate_integral_tree('
lacks src/target/riscv64/abi.c 'minic_riscv64_integer_aggregate_member_type('

# Exhaustive Common Core semantic vocabulary must be used by all five targets.
exists src/target/common/core_instruction_family.h
has src/target/common/core_instruction_family.h 'minic_core_instruction_semantic_family('
for consumer in src/target/x86_64/machine_emit.c \
                src/target/riscv32/rv32_emit.c \
                src/target/riscv64/core_admission.c \
                src/target/common/direct_instruction_semantics.c; do
  has "$consumer" 'minic_core_instruction_semantic_family('
done
lacks src/target/common/direct_instruction_semantics.c 'minic_multi_direct_memory_machine_kind('
has src/target/common/program_visit.h 'minic_program_function_selected('
has src/target/common/core_function_plan.c 'minic_core_function_verify(function)'
has src/target/riscv64/core_admission.c 'RV64 atomic ISA/memory-order lowering is not qualified yet.'

# Five architectures must share one ELF alias behavior; target-specific
# assembler spelling belongs to the target configuration and must not leak
# architecture conditionals back into the common program visitor.
exists src/target/common/elf_alias_emit.h
has src/target/common/elf_alias_emit.h 'minic_multi_elf_emit_alias('
has src/target/common/elf_alias_emit.h 'minic_multi_elf_emit_object_alias('
has src/target/riscv/function_symbol.h 'minic_multi_elf_emit_alias('
has src/target/riscv64/codegen_function.c 'minic_multi_elf_emit_object_alias('
has src/target/x86_64/codegen.c 'minic_multi_elf_emit_object_alias('
has src/target/common/direct_program_emit.c 'ctx->config->elf_function_type'
has src/target/common/direct_program_emit.c 'ctx->config->elf_object_type'
lacks src/target/common/direct_program_emit.c 'MINIC_TARGET_ARCH_AARCH64'
has src/target/aarch64/codegen.c '"%function", "%object"'
has src/target/aarch64/asm_contract.c 'aarch64_barrier_template('
# Genuine Linux privileged alternative patch metadata and GNU/Q operand
# ownership remain target-local. Common Core knows only output/memory input
# operand roles and must never fake a TPIDR register load or patch table.
has src/target/aarch64/asm_contract.c 'aarch64_linux_percpu_alternative_template('
has src/target/aarch64/asm_contract.c '.pushsection .altinstructions'
has src/target/aarch64/asm_contract.c '"bic", ".if"'
has src/target/aarch64/asm_contract.c 'aarch64_atomic_operations[op].name'
has src/target/aarch64/asm_contract.c 'aarch64_linux_percpu_rmw_form('
has src/target/aarch64/asm_contract.c 'p.op->alu'
lacks src/core/core_lower_asm_target.c 'ldxrb'
has src/target/aarch64/asm_contract.c 'aarch64_atomic_return_form('
has src/target/aarch64/asm_contract.c 'ret.op->alu'
lacks src/core/core_lower_asm_target.c 'atomic64_add_return'
has src/target/aarch64/asm_contract.c 'aarch64_linux_xchg_form('
has src/target/aarch64/asm_contract.c 'aarch64_xchg_ld[xchg.mode]'
lacks src/core/core_lower_asm_target.c 'ldaxrb'
has src/target/aarch64/asm_contract.c 'aarch64_atomic_form('
has src/target/backend.c 'minic_backend_inline_asm_prefix_matches('
has src/frontend/parser_local_declarations.c 'consume_static_local_interleaved_attribute, attributes'
has src/frontend/parser_function.c 'has_const_qualifier'
has src/frontend/parser_function.c '!minic_type_add_const(base_type, &base_type)'
has src/frontend/parser_function.c '!minic_type_add_volatile(base_type, &base_type)'
has src/target/target_info.c "name[0] == '%'"
lacks src/core/core_lower_asm_target.c 'discard.addressable'
has src/target/x86_64/asm_contract.c '"cmp%1,%2;sbb%0,%0;"'
has src/target/x86_64/asm_contract.c 'MINIC_X86_BIT_SCAN_BSR32_TIED'
has src/target/x86_64/asm_contract.c 'MINIC_X86_BIT_SCAN_BSR64_READWRITE'
has src/target/x86_64/asm_contract.c 'x86_linux_atomic_cc_source('
has src/target/x86_64/asm_contract.c 'x86_trycas_source('
has src/target/x86_64/asm_contract.c 'src->input_count == 2U'
has src/target/x86_64/machine_asm.c 'operand_count == 6U'
lacks src/core/core_lower_asm_target.c 'native_cpuid'
has src/target/x86_64/machine_asm.c 'emit_linux_trycas('
lacks src/core/core_lower_asm_target.c 'cmpxchgb'
has src/target/x86_64/asm_contract.c 'MINIC_X86_ATOMIC_CC_ADD64_SIGN'
has src/target/x86_64/asm_contract.c 'mem_type.integer_rank != MINIC_INTEGER_RANK_CHAR'
has src/target/x86_64/asm_contract.c 'x86_atomic_cc_forms[]'
lacks src/core/core_lower_asm_target.c 'MINIC_X86_ATOMIC_CC_SUB32_ZERO'
has src/target/x86_64/asm_contract.c 'x86_linux_bitops_source('
has src/target/x86_64/machine_asm.c 'emit_linux_bitops('
lacks src/core/core_lower_asm_target.c 'btcq'
has src/target/x86_64/machine_asm.c 'emit_linux_atomic_cc('
lacks src/core/core_lower_asm_target.c '=@cce'
has src/target/x86_64/machine_asm.c 'emit_linux_tied_bit_scan'
lacks src/core/core_lower_asm_target.c 'bsrl'
lacks src/core/core_lower_asm_target.c 'sbb %0,%0'
lacks src/target/backend.c 'aarch64_qualified_inputs'
lacks src/target/backend.c 'loongarch64_qualified_inputs'
lacks src/target/backend.c 'x86_64_qualified_inputs'
lacks src/target/backend.h 'qualified_input_mnemonic_count'
lacks src/core/core_lower_asm_target.c 'tpidr_el1'
lacks src/core/core_lower_asm_target.c '.altinstructions'
has src/target/backend.c 'minic_loongarch64_qualify_inline_asm'
has src/target/loongarch64/residual_emit.c 'minic_loongarch64_emit_qualified_structured_asm'
has src/core/core_lower_asm_scalar_forms.c 'e->kind == MINIC_EXPRESSION_FIXED_REGISTER'
has src/core/core_lower_asm_scalar_forms.c 'minic_target_info_fixed_register_write_supported('
has src/target/loongarch64/asm_contract.c 'loongarch64_dbar_hint'
has src/target/loongarch64/asm_contract.c 'loongarch64_dbar_hint'
has src/target/loongarch64/codegen.c '"@function", "@object"'
# A front-end GNU alias is bound once after the entire TU, never by a
# per-architecture parser special case or a fabricated declaration.
has src/frontend/parser_function.c 'parser_resolve_pending_function_aliases('
has src/frontend/ast_verifier.c 'minic_target_info_function_alias_abi_compatible('
lacks src/frontend/parser_function.c 'MINIC_TARGET_ARCH_LOONGARCH64'

# The Common block cursor selects the identical lexical Core order across
# all five backends; only machine instruction/terminator handlers are private.
exists src/target/common/core_block_walk.h
for consumer in src/target/x86_64/codegen.c \
                src/target/riscv32/rv32_emit.c \
                src/target/riscv64/core_codegen.c; do
  has "$consumer" 'minic_core_block_walk_next('
  has "$consumer" 'minic_core_block_instruction_at('
done
# Direct LP64 machine dispatch now has one Common owner: each ISA supplies
# only native family callbacks, GP transport and terminal instructions.
exists src/target/common/direct_machine_dispatch.h
has src/target/common/direct_machine_dispatch.h 'minic_core_block_walk_next('
has src/target/common/direct_machine_dispatch.h 'minic_core_block_instruction_at('
has src/target/common/direct_machine_dispatch.h 'minic_multi_direct_machine_instruction_shape('
has src/target/common/direct_machine_dispatch.h 'shape.family != MINIC_DIRECT_MACHINE_INTEGER'
# Common direct-LP64 ELF/FunctionPlan/orchestration now has ONE owner;
# AArch64 and LoongArch64 supply only target-specific prologue/ISA hooks.
exists src/target/common/direct_function_emit.h
has src/target/common/direct_function_emit.h 'minic_multi_direct_lp64_function_plan_prepare('
has src/target/common/direct_function_emit.h 'minic_multi_emit_elf_symbol_visibility('
has src/target/common/direct_function_emit.h 'config->emit_prologue(emission, &ctx)'
has src/target/common/direct_function_emit.h 'config->emit_core_blocks(emission, &ctx)'
has src/target/common/direct_function_emit.h 'config->rewrite_dynamic_asm(out, staged)'
for arch in aarch64 loongarch64; do
  file="src/target/$arch/codegen.c"
  has "$file" 'minic_multi_direct_lp64_emit_function('
  lacks "$file" 'minic_multi_direct_lp64_function_plan_prepare('
  lacks "$file" 'minic_multi_emit_elf_symbol_visibility('
done
# Persistent source fingerprints: reviewed code never silently falls back to
# "already scanned" after a source change. New source owners must be indexed.
python3 tools/ci/minic-backend-review-ledger.py --check
for arch in aarch64 loongarch64; do
  block="src/target/$arch/block_emit.c"
  has "$block" 'minic_multi_direct_machine_dispatch('
  has "$block" "static bool ${arch}_machine_emit_family("
  lacks "$block" 'minic_core_block_walk_next('
done
has src/target/common/core_function_facts.c 'facts->variadic_address_results = *variadic_results;'
has src/target/common/core_function_plan.c 'minic_core_function_facts_analyze_collect('
lacks src/target/common/core_function_plan.c 'for (i = 0U; i < function->instruction_count;'
has src/target/x86_64/frame.c 'facts->variadic_address_results[i]'
has src/target/common/program_emit.h 'descriptor = mkstemp(temporary);'
has src/target/common/program_emit.h 'rename(temporary, path)'
lacks src/target/riscv/function_symbol.h 'MinicRiscv64FunctionSymbol'
has src/target/riscv/function_symbol.h 'MinicRiscvFunctionSymbol'

# The RV64 ABI classification+placement plan, not its Core instruction
# admission, owns return/argument admissibility and rejection rollback.
has src/target/riscv64/function_abi_plan.c \
  'minic_riscv64_function_abi_plan_qualified('
lacks src/target/riscv64/core_admission.c 'return_value.kind =='
lacks src/target/riscv64/core_admission.c 'location.value.kind =='

lacks src/target/riscv64/core_codegen.c 'core_instruction_supported('
lacks src/target/riscv64/core_codegen.c \
  'minic_riscv64_object_liveness_begin_function('
for obsolete in object_intervals.c object_intervals.h object_liveness.c \
                object_liveness.h spill_layout.c spill_layout.h; do
  [[ ! -e "src/target/riscv64/$obsolete" ]] ||
    fail "obsolete RV64 wrapper survived: $obsolete"
done
lacks src/target/x86_64/machine_emit.c \
  'minic_x86_64_incoming_cursor('
# The mature x86 SysV backend shares the same prepared sparse CALL owner
# used by RV32/RV64 and Direct LP64, without exporting its psABI to Core.
has src/target/x86_64/frame.c 'minic_x86_64_outgoing_call_plans_prepare('
has src/target/x86_64/machine_call.c 'x86_resolve_call_view('
lacks src/target/x86_64/call_plan.c 'while (low < high)'

for arch in aarch64 loongarch64; do
  block="src/target/$arch/block_emit.c"
  residual="src/target/$arch/residual_emit.c"
  header="src/target/$arch/residual_emit.h"
  for file in "$block" "$residual" "$header"; do exists "$file"; done
  has "$block" 'minic_multi_direct_machine_dispatch('
  has "$block" "minic_${arch}_emit_residual_instruction("
  scalar="src/target/$arch/scalar_normalize.c"
  exists "$scalar"
  has "$scalar" "bool minic_${arch}_emit_integer_normalize("
  has "$residual" "minic_${arch}_emit_integer_normalize("
  lacks "$residual" 'residual_integer_normalize('
  has "$residual" 'minic_multi_direct_integer_cast_plan('
  lacks "$block" 'static bool emit_integer_normalize('
  lacks "$block" 'minic_multi_direct_result_slot_offset('
  lacks "$header" '(*normalize)'
  lacks "$residual" '(*normalize)'
  lacks "$block" "minic_${arch}_emit_integer_normalize"
  for owner in fp_emit integer_emit call_emit; do
    has "src/target/$arch/$owner.c" "minic_${arch}_emit_integer_normalize("
    lacks "src/target/$arch/$owner.c" '(*normalize)'
    lacks "src/target/$arch/$owner.h" '(*normalize)'
  done
  if grep -Eq '^[[:space:]]*case[[:space:]]+MINIC_CORE_INSTRUCTION_' "$block"; then
    fail "$block has residual Core cases; native cases belong in family owners"
  fi
done

# Invocation-specific language ABI flags must not mutate canonical models.
has src/target/target_info.c 'minic_target_invocation_begin('
has src/target/target_info.c 'minic_target_invocation_plain_char('
has src/target/target_info.c 'minic_target_invocation_short_wchar('
has tools/minic-cc/main.c 'minic_target_invocation_begin('
has tools/minic/main.c 'minic_driver_plain_char_unsigned('
has tools/minic/main.c 'language.relocation != NULL'
lacks tools/minic-cc/main.c 'selected_target.wide_character_type ='
exists tests/compiler/c0/probe-target-language-abi-options.sh

# ISA capability is not C language validity. Both XLEN backends must
# consume the same single-word arithmetic selector, and the original RV32
# helper fallback is retained until an ISA extension is actually qualified.
exists src/target/riscv/isa_features.h
has src/target/riscv64/isa.h 'target/riscv/isa_features.h'
has src/target/target_info.h 'target/riscv/isa_features.h'
lacks src/target/target_info.h 'target/riscv64/isa.h'
exists src/target/riscv/isa_resolve.h
exists src/target/riscv/isa_resolve.c
has src/target/riscv/isa_resolve.c 'bool minic_riscv_isa_resolve('
lacks src/target/riscv64/isa.c 'bool minic_riscv_isa_resolve('
has tools/minic-cc/main.c 'target/riscv/isa_resolve.h'
lacks src/target/riscv64/isa.h 'typedef enum MinicRiscvFeature'
# The old, unused ABI admission functions were misleading duplicates of
# the live target/ABI planner. They must not be restored.
lacks src/target/riscv64/isa.h 'minic_riscv_isa_has('
lacks src/target/riscv64/isa.h 'minic_riscv_isa_lp64_integer_signature_supported('
lacks src/target/riscv64/isa.c 'abi_type_contains_float('
has src/target/riscv/instruction_select.h 'minic_riscv_plan_integer_arithmetic('
has src/target/riscv64/runtime_libcall.c 'minic_riscv_plan_integer_arithmetic('
has src/target/riscv32/rv32_integer_emit.c 'minic_riscv_plan_integer_arithmetic('
# R10: pointer-scale semantic classification is shared; target-specific
# call/return-address mechanics remain in their own RV32/RV64 backends.
has src/target/riscv/instruction_select.h 'minic_riscv_plan_pointer_scale('
has src/target/riscv32/rv32_memory_emit.c 'minic_riscv_plan_pointer_scale('
has src/target/riscv32/rv32_memory_emit.c 'MINIC_RISCV_POINTER_SCALE_SHIFT'
has src/target/riscv32/rv32_memory_emit.c 'MINIC_RISCV_POINTER_SCALE_MULTIPLY'
lacks src/target/riscv32/rv32_memory_emit.c 'if (stride != 1U)'
has src/target/riscv64/core_codegen.c 'minic_riscv_plan_pointer_scale('
has src/target/riscv64/runtime_libcall.c 'minic_riscv_plan_pointer_scale('
lacks src/target/riscv64/runtime_libcall.h 'minic_riscv64_power_of_two('
lacks src/target/riscv64/runtime_libcall.c 'minic_riscv64_log2_power_of_two('
lacks src/target/riscv64/core_codegen.c 'minic_riscv64_power_of_two('

has src/target/riscv32/rv32_frame.c 'frame->isa = &t->riscv_isa'
has src/target/riscv32/rv32_emit.c 'rv32_emit_integer_instruction('
# RV32/RV64 share machine-neutral ELF symbol semantics at the common owner.
exists src/target/riscv/function_symbol.h
[[ ! -e src/target/riscv64/function_symbol.h ]] || fail 'duplicate old RV64 symbol owner'
for user in src/target/riscv32/rv32_emit.c \
            src/target/riscv64/global_emit.c \
            src/target/riscv64/codegen_function.c \
            src/target/riscv64/core_codegen.h; do
  has "$user" 'target/riscv/function_symbol.h'
  lacks "$user" 'target/riscv64/function_symbol.h'
done

# ISA/ABI profile names must be shared between native compiler and staged
# product driver. Only machine legalization owns the actual feature bits.
has src/target/riscv/isa_resolve.c 'minic_riscv_profile_find(march)'
has tools/minic/main.c 'minic_riscv_profile_find(march)'
has src/target/target_catalog.c 'minic_riscv_profile_all('
lacks tools/minic/main.c 'strcmp(march, "rv64im")'

# A single catalog owns target spelling for driver, compiler and TargetInfo.
# The product driver must not silently select RV64 AS/LD for other ISAs.
exists src/target/target_catalog.c
exists src/target/target_catalog.h
has src/target/target_info.c 'minic_target_catalog_find(name)'
has tools/minic/main.c 'minic_target_catalog_find(target_name)'
has tools/minic-cc/main.c 'minic_target_catalog_all(&count)'
has src/target/target_catalog.c 'MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER'
lacks tools/minic/main.c 'strcmp(target_name, "riscv64-linux")'
lacks src/target/target_info.c 'strcmp(name, "riscv64-linux")'

# The frontend may query C language capabilities, but it must never grow
# another explicit CPU allowlist when an ISA is added.
if grep -REq 'MINIC_TARGET_ARCH_' src/frontend; then
  fail 'frontend must query TargetInfo language features, not CPU names'
fi
has src/target/target_info.c 'minic_target_info_language_type_supported('
has src/frontend/parser_type.c 'minic_target_info_named_builtin_type('
has src/frontend/ast_verifier.c 'minic_target_info_language_type_supported('

# New capabilities must extend the existing Backend/Target contract, not
# introduce ISA switches into Core or duplicate GNU template qualification.
for file in src/target/x86_64/asm_contract.c src/target/x86_64/asm_contract.h \
            src/core/core_lower_asm_target.c; do exists "$file"; done
has src/target/backend.c 'minic_x86_64_qualify_inline_asm'
has src/core/core_lower_asm_target.c 'minic_backend_qualify_inline_asm('
has src/target/x86_64/machine_asm.c 'minic_x86_64_asm_mulacc_template('
has src/target/x86_64/machine_asm.c 'minic_x86_64_asm_matched_template('
lacks src/core/core_lower_asm.c 'core_asm_try_x86_register_contract('
lacks src/core/core_lower_varargs.c 'MINIC_TARGET_ARCH_X86_64'
has src/core/core_lower_varargs.c 'MINIC_VARIADIC_COPY_ABI_STATE'
has src/target/target_info.h 'MinicVariadicCopyKind'
if [[ -e src/core/core_lower_asm_x86.c ]]; then
  fail 'legacy x86-specific Core classifier must not return'
fi

# Compiler-owned RV64I instructions must be audited separately from the
# GNU source asm that the original Linux Kbuild assembler is responsible for.
has src/target/riscv64/isa.c 'if (in_user_asm) continue;'
has src/target/riscv64/isa.c 'minic_riscv64_asm_audit_marker('
has src/target/riscv64/codegen_function.c 'minic_riscv64_emit_file_asm('
lacks src/target/riscv64/codegen_function.c 'inline assembly is not yet ISA-qualified'
lacks src/target/riscv64/codegen_function.c 'file-scope inline assembly is not qualified'
exists tests/compiler/c0/probe-rv64i-asm-origin.sh

# The RISC-V family owns one XLEN-independent implementation of pair
# add/sub/logic/unary and machine-word bit scans. Neither ISA may silently
# grow a duplicate carry/borrow or CLZ/CTZ emitter.
exists src/target/riscv/word_operations.c
exists src/target/riscv/word_operations.h
has src/target/riscv32/rv32_wide_emit.c 'minic_riscv_emit_word_pair_binary('
has src/target/riscv32/rv32_wide_emit.c 'minic_riscv_emit_word_pair_unary('
has src/target/riscv32/rv32_integer_emit.c 'minic_riscv_emit_word_bitscan('
has src/target/riscv64/scalar_emit.c 'minic_riscv_emit_word_pair_binary('
has src/target/riscv64/scalar_emit.c 'minic_riscv_emit_word_pair_unary('
has src/target/riscv64/scalar_emit.c 'minic_riscv_emit_word_bitscan('
has src/target/riscv32/rv32_wide_emit.c 'minic_riscv_emit_word_pair_shift('
has src/target/riscv64/scalar_emit.c 'minic_riscv_emit_word_pair_shift('
lacks src/target/riscv32/rv32_wide_emit.c '  sll t1, t1, t2'
lacks src/target/riscv64/scalar_emit.c '  sll t1, t1, t2'
lacks src/target/riscv32/rv32_wide_emit.c '  sltu t5, t4, t0'
lacks src/target/riscv64/scalar_emit.c '  sltu t5, t4, t0'

# RV32 physical machine code lives under its own ILP32 target, not RV64.
for file in src/target/riscv32/rv32_emit.c \
            src/target/riscv32/rv32_frame.c \
            src/target/riscv32/rv32_scalar.c \
            src/target/riscv32/rv32_integer_emit.c \
            src/target/riscv32/rv32_wide_emit.c \
            src/target/riscv32/rv32_fp_emit.c \
            src/target/riscv32/rv32_memory_emit.c \
            src/target/riscv32/rv32_call.c \
            src/target/riscv32/rv32_abi_plan.c \
            src/target/riscv32/rv32_abi_plan.h \
            src/target/riscv32/rv32_internal.h \
            src/target/riscv/gpr_abi_cursor.h \
            src/target/riscv/record_copy_bytes.c; do
  exists "$file"
done
if compgen -G 'src/target/riscv64/rv32_*' >/dev/null; then
  fail 'RV32 machine implementation was reintroduced under RV64'
fi
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_view_build('
lacks src/target/riscv32/rv32_emit.c 'static bool rv32_frame_init('
lacks src/target/riscv32/rv32_emit.c 'static bool rv32_emit_scalar_call_arguments('
lacks src/target/riscv32/rv32_emit.c 'rv32_call_info('
has src/target/riscv32/rv32_abi_plan.c 'minic_riscv32_abi_place('
has src/target/riscv32/rv32_scalar.c 'rv32_scalar_storage_words('
has src/target/riscv/instruction_select.h 'minic_riscv_integer_arithmetic_symbol('
has src/target/riscv32/rv32_integer_emit.c 'rv32_emit_wide_integer('
has src/target/riscv32/rv32_wide_emit.c 'minic_riscv_integer_arithmetic_symbol('
lacks src/target/riscv32/rv32_integer_emit.c '  add t4, t0, t2'
has src/target/riscv32/rv32_fp_emit.c 'minic_riscv_softfloat_arithmetic_symbol('
has src/target/riscv32/rv32_memory_emit.c 'minic_riscv_emit_record_copy_bytes('
has src/target/riscv32/rv32_call.c 'rv32_emit_parameter('
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_view_build('
has src/target/riscv32/rv32_abi_plan.c 'minic_riscv32_abi_place('
has src/target/riscv32/rv32_frame.c 'rv32_function_abi_plan_build('
has src/target/riscv32/rv32_frame.c 'minic_core_object_reuse_plan_build('
exists src/target/common/core_object_reuse.c
exists src/target/common/core_object_intervals.c
has src/target/riscv64/frame_layout.c 'minic_core_object_reuse_plan_build('
exists src/target/common/core_spill_layout.c
exists src/target/common/core_spill_layout.h
has src/target/riscv32/rv32_abi_plan.c 'rv32_abi_reject("incoming_return"'
# RV32 shares the compact canonical CALL index. Never allocate one
# heavyweight machine call plan for each non-CALL instruction again.
has src/target/riscv32/rv32_abi_plan.c 'minic_core_instruction_is_call('
lacks src/target/riscv32/rv32_abi_plan.c 'while (low < high)'
has src/target/riscv32/rv32_abi_plan.c 'calls->sites[ordinal].instruction_id'
has src/target/riscv64/frame_requirements.c 'calls->sites[ordinal]'
# The fifth target no longer has a private Core VALUE/OBJECT type resolver:
# CoreCallView checks real argument provenance and the RV64 psABI decides
# fixed declared types versus unnamed promoted argument placement.
has src/target/riscv64/frame_requirements.c 'minic_core_call_view_argument_type('
has src/target/riscv64/call_emit.c 'minic_core_call_view_argument_type('
lacks src/target/riscv64/frame_requirements.c 'bool minic_riscv64_core_call_argument_type('
lacks src/target/riscv64/frame_requirements.h 'bool minic_riscv64_core_call_argument_type('
# The Direct LP64 pair must also use a compact real CALL site cache.
lacks src/target/common/direct_call_plan.c 'cache->by_instruction'
has src/target/common/direct_call_plan.c 'cache->sites[ordinal]'
has src/target/common/direct_call_plan.c 'target/common/core_instruction_family.h'
lacks src/target/common/direct_call_plan.c 'while (low < high)'
lacks src/target/riscv32/rv32_abi_plan.c 'plan->call_count = function->instruction_count'
has src/target/riscv32/rv32_abi_plan.h 'size_t instruction_id;'
lacks src/target/riscv32/rv32_emit.c 'rv32_scalar_storage_words(f, f->return_type, &bits)'
has src/target/riscv32/rv32_frame.c 'varargs_offset'
has src/target/riscv32/rv32_emit.c 'MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS'
has src/target/target_info.c 'rv32_target.variadic_linear_slot_bytes = 4U'
lacks src/target/riscv32/rv32_emit.c 'defining variadic functions requires ILP32 va_list ABI'
has src/target/riscv32/rv32_frame.c 'max_outgoing_stack_words'
lacks src/target/riscv32/rv32_frame.c 'minic_core_function_facts_analyze('
has src/target/riscv32/rv32_frame.c 'rv32_fixed_stack('
has src/target/riscv32/rv32_memory_emit.c 'MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC'
has src/target/riscv32/rv32_emit.c 'fr.has_dynamic_stack'
has src/target/riscv32/rv32_call.c 'rv32_function_abi_call('
lacks src/target/riscv32/rv32_frame.c 'minic_core_call_view_build('
lacks src/target/riscv32/rv32_call.c 'minic_riscv32_abi_place('
lacks src/target/riscv32/rv32_call.c 'minic_core_call_view_build('
lacks src/target/riscv32/rv32_call.c 'for (size_t j = 0U; j < idx; ++j)'
has src/target/riscv32/rv32_emit.c 'rv32_emit_integer_instruction('
has src/target/riscv32/rv32_emit.c 'rv32_emit_fp_instruction('
has src/target/riscv32/rv32_emit.c 'rv32_emit_memory_instruction('
lacks src/target/riscv32/rv32_emit.c 'call __mulsi3'
lacks src/target/riscv32/rv32_emit.c 'call __adddf3'
lacks src/target/riscv32/rv32_emit.c 'minic_riscv_emit_record_copy_bytes('

has src/frontend/parser_expression.c 'thread-local object access requires target TLS relocation'
has src/target/riscv32/rv32_global_emit.c 'rv32_array_of_records('

# Full-source global coverage: a change to ANY frontend/IR/compiler/target
# .c or .h must reach the four-runtime-target matrix, on PR and main.
# Explicit historical owner lists alone previously missed target FP,
# memory, frame, ABI and x86 machine family changes.
for trigger in 'src/**' 'include/minic/**' 'tools/minic/**' \
               'tools/minic-cc/**' 'Makefile'; do
  count=$(grep -Fxc -- "      - \"$trigger\"" \
          .github/workflows/minic-runtime-fourarch.yml || true)
  [[ "$count" -ge 2 ]] || fail "four-arch PR/push missing trigger $trigger count=$count"
done
# PR and main ownership symmetry for both architecture-specific regressions.
# Keep this apt-free: the runner already has Python, not necessarily PyYAML.
python3 - <<'PY'
from pathlib import Path
owners = {
    'src/frontend/**', 'src/core/**', 'src/compiler/**',
    'src/target/common/**', 'src/target/target_info*',
    'src/target/data_layout*', 'include/minic/**',
    'tools/minic/**', 'tools/minic-cc/**',
}
for name in ('minic-rv32-cjson-runtime.yml', 'minic-rv64i-origin-audit-seven.yml'):
    source = (Path('.github/workflows') / name).read_text()
    for event, stop in (('pull_request', 'push'), ('push', 'workflow_dispatch')):
        parts = source.split('  ' + event + ':\n', 1)
        if len(parts) != 2:
            raise SystemExit(f'MINIC_CI_OWNER=FAIL {name} event={event}')
        section = parts[1].split('  ' + stop + ':\n', 1)[0]
        paths = {line.strip()[2:].strip().strip("'\"")
                 for line in section.splitlines()
                 if line.strip().startswith('- ')}
        missing = sorted(owners - paths)
        if missing:
            raise SystemExit(f'MINIC_CI_OWNER=FAIL {name} event={event} missing={missing}')
print('MINIC_CI_OWNER=PASS rv32_rv64_shared_source_PR_and_main')
PY

# Register placement must not leak from one machine into TargetInfo's
# architecture-neutral C language ABI model. Production callers were zero.
lacks src/target/target_info.h 'minic_riscv64_argument_registers'
lacks src/target/target_info.c 'minic_riscv64_argument_registers'
# Frontend and Core lower language semantics, never switch on CPU enums.
if grep -REq 'MINIC_TARGET_ARCH_' src/frontend src/core; then
  fail 'frontend/Core must not add machine architecture switches'
fi
for backend in riscv32 riscv64 x86_64 aarch64 loongarch64; do
  has src/target/backend.c "MINIC_TARGET_ARCH_$(echo "$backend" | tr '[:lower:]' '[:upper:]')"
done

# Global program traversal is not an AArch64/LoongArch-only subsystem.
# All five machine backends must use the same common object/function visitor
# while retaining target-owned ABI, assembler output and source-ASM policy.
exists src/target/common/program_visit.h
# Shared Core opcode-family classification feeds RV32, RV64, x86/Direct
# without claiming a universal machine ISA or ignoring target admission.
exists src/target/common/core_instruction_family.h
# Common's public headers expose only true cross-unit dependencies.
# RV64 frame borrows/owns the same semantic CALL index as all other targets.
has src/target/riscv64/frame_layout.h 'MinicCoreCallIndexHandle call_index;'
has src/target/riscv64/frame_layout.c 'minic_core_call_index_handle_prepare('
has src/target/riscv64/frame_layout.c 'minic_core_call_index_handle_destroy('
lacks src/target/riscv64/frame_layout.h 'standalone_calls'
lacks src/target/riscv64/frame_layout.c 'minic_core_call_index_build('
lacks src/target/common/direct_instruction_semantics.h 'minic_multi_direct_compiler_barrier_shape('
lacks src/target/common/lp64_scalar.h 'minic_lp64_small_integer_width('
lacks src/target/common/global_emit.h 'minic_multi_emit_record_bit_field_run('
lacks src/target/common/global_emit.h 'minic_multi_recursive_shape_supported('
lacks src/target/common/global_emit.h 'minic_multi_emit_flexible_record_global('

exists src/target/common/core_numeric_shape.h
# Three machine frame families now consume one policy-qualified Common
# SSA slot planner: SysV cumulative 8/16, Direct LP64 wide sidecar, and
# the earlier RV32/RV64 block-local last-use planner.
exists src/target/common/core_value_slots.c
exists src/target/common/core_value_slots.h
has src/target/common/core_value_slots.c 'bool minic_core_value_slots_build('
has src/target/x86_64/frame.c 'MINIC_CORE_VALUE_SLOT_LINEAR'
has src/target/common/lp64_frame.c 'MINIC_CORE_VALUE_SLOT_WIDE_TAIL'
lacks src/target/x86_64/frame.c 'value_bytes += slot;'
lacks src/target/common/lp64_frame.c 'plan->wide_offsets[i] = pos;'
exists src/target/common/core_numeric_shape.c
has src/target/common/core_numeric_shape.c 'bool minic_core_integer_overflow_shape('
has src/target/common/core_numeric_shape.c 'bool minic_core_integer_bitscan_shape('
has src/target/x86_64/machine_emit.c 'minic_core_integer_bitscan_shape('
has src/target/riscv64/scalar_emit.c 'minic_core_integer_bitscan_shape('
has src/target/aarch64/integer_emit.c 'minic_core_integer_overflow_shape('
has src/target/loongarch64/integer_emit.c 'minic_core_integer_overflow_shape('
lacks src/target/common/direct_instruction_semantics.c 'minic_multi_direct_fp_instruction_kind('
lacks src/target/common/direct_instruction_semantics.h 'minic_multi_direct_integer_overflow_shape('
has src/target/riscv32/rv32_emit.c 'MINIC_CORE_FAMILY_INTEGER_SCALAR'
has src/target/riscv64/core_admission.c 'MINIC_CORE_FAMILY_INTEGER_SCALAR'
has src/target/riscv64/core_codegen.c 'minic_core_instruction_is_floating_scalar(instruction->kind)'
has src/target/common/direct_instruction_semantics.c 'MINIC_CORE_FAMILY_FLOATING_SCALAR'
has src/target/common/program_visit.h 'minic_program_visit_globals('
has src/target/common/program_visit.h 'minic_program_visit_functions('
has src/target/common/program_visit.h 'minic_program_visit_prepared_functions('
# All five machine backends must borrow one visitor-owned per-function Core
# object plan, not repeat layout scans. Each target keeps its own psABI frame.
has src/target/common/core_function_plan.c 'minic_core_object_storage_plan_build('
has src/target/common/core_function_plan.c 'minic_core_object_storage_plan_destroy(&plan->objects)'
has src/target/x86_64/codegen.c 'minic_x86_64_frame_init_with_storage('
has src/target/riscv32/rv32_emit.c 'rv32_frame_init_with_storage('
has src/target/riscv64/codegen_function.c 'minic_riscv64_emit_core_function_for_program_with_symbol_with_storage('
has src/target/aarch64/codegen.c 'minic_multi_direct_lp64_emit_function('
has src/target/loongarch64/codegen.c 'minic_multi_direct_lp64_emit_function('
# All four physical frame owners use the single Common borrowing contract.
# Borrowed immutable entries must never be destroyed by target-local code.
has src/target/common/core_object_storage.c '*result = *shared;'
has src/target/common/core_object_storage.c '*borrowed = true;'

# Unique program-wide DataLayout validation supersedes the obsolete
# AST/RV64-only layout pass and requires no backend prepare callback.
exists src/target/data_layout.c
has src/compiler/compiler.c 'minic_data_layout_validate_program(target_info->data_layout, &program)'
lacks src/compiler/compiler.c 'backend->prepare_program'
lacks src/target/backend.h 'MinicBackendPrepareProgram'
lacks src/target/riscv64/codegen_internal.h 'target/riscv64/layout.h'
has src/target/data_layout.c 'MinicDataLayoutProgramCheck minic_data_layout_validate_program('
for obsolete in src/target/riscv64/layout.c src/target/riscv64/layout.h; do
  [[ ! -e "$obsolete" ]] || fail "old RV64 AST layout survived: $obsolete"
done
# Actual five-target frame ownership, not another architecture-specific
# Core storage calculator. The target still chooses ABI stack geometry.
exists src/target/common/core_frame_layout.h
has src/target/common/core_object_storage.c 'return minic_core_object_storage_plan_build(program, layout, fn, result);'
has src/target/riscv32/rv32_frame.c 'minic_core_frame_reserve('
# Entire RISC-V Core SSA frame planning now uses one pass across blocks,
# not a peak pass followed by an identical assignment re-traversal.
# Common owns both the peak extent and all value-id offsets.
has src/target/common/core_spill_layout.c 'bool minic_core_function_reused_spill_layout('
has src/target/common/core_spill_layout.h 'bool minic_core_function_reused_spill_layout('
lacks src/target/common/core_spill_layout.c 'minic_core_function_reused_spill_peak('
lacks src/target/common/core_spill_layout.c 'minic_core_function_reused_spill_assign('
has tests/compiler/c0/run-core-block-spill-reuse.sh 'output-rv32.s'
has .github/workflows/minic-native-refactor-gate.yml 'Two-XLEN Common single-pass SSA spill regressions'
for backend in src/target/riscv32/rv32_frame.c src/target/riscv64/frame_layout.c; do
  has "$backend" 'minic_core_function_reused_spill_layout('
  lacks "$backend" 'minic_core_function_reused_spill_peak('
  lacks "$backend" 'minic_core_function_reused_spill_assign('
  lacks "$backend" 'minic_core_block_reused_spill_layout('
done
has src/target/common/lp64_frame.c 'minic_core_frame_reserve('
has src/target/riscv64/frame_layout.c 'minic_core_frame_align_up('
has src/target/x86_64/frame.c 'minic_core_frame_align_up('
lacks src/target/riscv32/rv32_frame.c 'minic_core_object_storage(program,'
lacks src/target/common/lp64_frame.c 'minic_core_object_storage(program,'

# The semantic CALL resolver is the single Core identity owner. psABI
# GP/FP/stack placement remains x86/RV32/RV64/AArch64/LoongArch-owned.
# Five targets get one immutable CallIndex with visitor-managed lifetime.
exists src/target/common/core_call_index.c
has src/target/common/core_function_plan.c 'minic_core_call_index_build('
has src/target/common/core_function_plan.c 'minic_core_call_index_destroy(&plan->calls)'
has src/target/common/program_visit.h 'minic_core_function_plan_build('
has src/target/common/program_visit.h 'minic_core_function_plan_destroy(&plan)'
has src/target/riscv32/rv32_frame.c 'facts->call_index'
has src/target/riscv64/frame_layout.c 'facts->call_index'
has src/target/common/direct_function_plan.c 'facts->call_index'
has src/target/common/core_call_view.c 'view.return_form = MINIC_CORE_CALL_RETURN_OBJECT'
has src/target/common/core_call_view.c 'minic_core_call_view_argument_type(function, &view, i, &actual)'
has src/target/x86_64/call_plan.c 'minic_core_call_view_argument_type(fn, view, i, &type)'
has src/target/x86_64/frame.c 'frame->semantic_calls = facts->call_index'
has src/target/x86_64/machine_call.c 'minic_core_call_index_find('
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_view_argument_type('
has src/target/common/direct_call_plan.c 'minic_core_call_view_argument_type(fn, &call, j, &actual)'
has src/target/riscv64/call_emit.c 'call->return_form != MINIC_CORE_CALL_RETURN_OBJECT'
has src/target/riscv64/frame_layout.c 'frame->semantic_calls = frame->call_index.index'
has src/target/riscv64/call_emit.c 'minic_core_call_index_find('
has src/target/riscv64/core_admission.c 'semantic_calls->sites[call_site++].view'
has src/target/riscv64/core_admission.c 'shape = &storage->objects[index]'
has src/target/riscv64/core_admission.c 'minic_core_object_storage('
has src/target/riscv64/core_codegen.c 'isa->abi == MINIC_RISCV_ABI_LP64, facts, storage)'
lacks src/target/riscv64/core_admission.c 'static bool core_global_addressable_type('
# Both production and standalone RV64 callers now have a prepared
# Common CALL index + psABI plan. The original per-CALL machine
# classifier, fallback view builder and cursor paths are dead and gone.
has src/target/riscv64/call_emit.c '&planned->arguments[argument_index]'
lacks src/target/riscv64/call_emit.c 'minic_riscv64_abi_place_argument('
lacks src/target/riscv64/call_emit.c 'minic_core_call_view_build('
lacks src/target/riscv64/call_emit.c 'minic_riscv64_abi_cursor_initialize_for_return_profile('
has src/target/riscv64/legalize.c '(call = *semantic_view, true)'
lacks src/target/riscv64/core_admission.c 'minic_type_is_pointer(instruction->type) && instruction->value.block_id'
lacks src/target/riscv64/core_admission.c 'function->globals[instruction->value.global_id].name != NULL'
has src/target/common/core_function_facts.c 'facts->max_call_arity = arity'
# Canonical verifier is shared, never independently rescanned per machine
# backend if the prepared visitor already admitted the exact Core function.
has src/target/common/core_function_plan.c 'minic_core_function_verify(function)'
lacks src/compiler/compiler.c '!minic_core_function_verify(&set->functions[function_index])'
has src/target/common/core_function_plan.c 'plan->facts.verified_function = function'
has src/target/x86_64/codegen.c 'minic_core_function_facts_verified(facts, fn)'
has src/target/common/direct_preflight.c 'minic_core_function_facts_verified(facts, fn)'
has src/target/riscv64/core_admission.c 'minic_core_function_facts_verified(facts, function)'
has src/target/common/core_function_plan.c 'minic_core_function_facts_analyze_collect('
# Borrow-or-own Common Core object layout is one lifecycle across all five
# physical frame plans, not repeated pointer/cleanup logic per backend.
has src/target/common/core_object_storage.c 'bool minic_core_object_storage_plan_acquire('
has src/target/common/core_object_storage.c 'void minic_core_object_storage_plan_release('
for frame in src/target/x86_64/frame.c src/target/riscv32/rv32_frame.c              src/target/riscv64/frame_layout.c src/target/common/lp64_frame.c; do
  has "$frame" 'minic_core_object_storage_plan_acquire('
  has "$frame" 'minic_core_object_storage_plan_release('
  lacks "$frame" 'frame->object_storage = *storage;'
  lacks "$frame" 'plan->object_storage = *shared;'
done
lacks src/target/common/direct_preflight.c 'term->branch_target >= fn->block_count'
lacks src/target/common/lp64_frame.c 'bool minic_lp64_frame_plan_objects('
lacks src/target/riscv64/frame_layout.c 'bool minic_riscv64_core_frame_initialize_prepared('
lacks src/target/x86_64/frame.c 'minic_x86_64_frame_init_prepared('
lacks src/target/riscv32/rv32_frame.c 'rv32_frame_init_prepared('
lacks src/target/riscv64/core_codegen.c 'minic_riscv64_emit_core_function_for_program_with_symbol_prepared('
has src/target/riscv64/frame_layout.c 'facts->needs_saved_return_address'
# All five targets now preplan real ABI CALL placement once. RV64 directly
# consumes the exact prepared psABI locations for machine CALL emission.
has src/target/riscv64/frame_layout.c 'minic_riscv64_call_plan_cache_build('
# RV64 standalone and production must converge on the one Common CALL index
# and one target ABI plan; the obsolete standalone sizing walker is deleted.
has src/target/riscv64/frame_layout.c 'minic_core_call_index_handle_prepare('
has src/target/riscv64/frame_layout.c 'minic_core_call_index_handle_destroy(&frame->call_index)'
lacks src/target/riscv64/frame_layout.c 'minic_riscv64_core_call_outgoing_stack_size_profile('
lacks src/target/riscv64/frame_requirements.c 'minic_riscv64_core_call_outgoing_stack_size_profile('
lacks src/target/riscv64/frame_requirements.h 'minic_riscv64_core_call_outgoing_stack_size('
has src/target/riscv64/frame_layout.c 'outgoing_calls.maximum_stack_bytes'
lacks src/target/riscv64/call_emit.c 'MinicRiscv64AbiCursor'
has src/target/riscv64/call_emit.c 'rv64_resolve_prepared_call('
# RV64 direct and indirect 128-bit returns must share the two-GPR store;
# a regression to the 64-bit scalar result silently loses a1.
has src/target/riscv64/call_emit.c 'static bool emit_core_call_wide_result('
has src/target/riscv64/legalize.c 'static bool rv64_wide_call_argument_supported('
has src/target/riscv64/legalize.c 'return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR'
has src/target/riscv64/abi.c 'minic_type_is_int128_integer(type)) && size == 16U'
lacks src/target/riscv64/abi.c '(lp64_soft_float && minic_type_is_int128_integer(type))'
if [[ "$(grep -Fc 'return emit_core_call_wide_result(' src/target/riscv64/call_emit.c)" != 2 ]]; then
  fail 'RV64 direct and indirect CALL no longer share dual-word return'
fi
has src/target/riscv64/frame_layout.c 'minic_riscv64_call_plan_cache_destroy('
lacks src/target/riscv64/frame_requirements.c 'while (low < high)'
# The RV64 named variadic prefix is qualified by the same immutable
# incoming function psABI plan. Do not duplicate the entire argument
# classifier in the old frame requirements compatibility wrapper.
has src/target/riscv64/core_admission.c 'minic_riscv64_abi_variadic_prefix_from_cursor('
has src/target/riscv64/core_admission.c '&abi_plan->named_parameter_cursor'
lacks src/target/riscv64/frame_requirements.c 'minic_riscv64_core_variadic_fixed_prefix_profile('
lacks src/target/riscv64/frame_requirements.h 'minic_riscv64_core_variadic_fixed_prefix('
has src/target/riscv64/frame_layout.c 'facts->uses_structured_inline_asm'
has src/target/riscv32/rv32_frame.c 'facts->uses_dynamic_stack_alloc'
has src/target/x86_64/frame.c 'facts->variadic_address_count'
has src/target/common/direct_function_profile.c 'facts->variadic_address_count'
has src/target/common/direct_function_profile.c 'facts->uses_dynamic_stack_alloc'
# Common CALL ordinal is the only sparse binary lookup for x86/RV64.
has src/target/common/core_call_index.c 'minic_core_call_index_find('
has src/target/common/core_call_index.c '*ordinal = low;'
lacks src/target/common/core_call_view.h 'minic_core_call_index_get('
lacks src/target/x86_64/call_plan.h 'minic_x86_64_outgoing_call_plan('
lacks src/target/riscv64/frame_requirements.h 'minic_riscv64_call_plan_cache_get('
lacks src/target/x86_64/machine_call.c 'minic_core_call_index_get('
lacks src/target/riscv64/call_emit.c 'minic_core_call_index_get('
has src/target/x86_64/machine_call.c '&frame->outgoing_call_plans.sites[ordinal].plan'
has src/target/riscv64/call_emit.c 'frame->outgoing_calls.sites[ordinal]'
lacks src/target/common/direct_instruction_semantics.h 'minic_multi_direct_memory_machine_kind('
lacks src/target/common/direct_instruction_semantics.h 'minic_multi_direct_abi_transfer_kind('
lacks src/target/common/direct_instruction_semantics.h 'minic_multi_direct_integer_machine_kind('
# Read-only Common CALL snapshots are consumed in ordinal order by both
# Direct LP64 ABIs and RV64 capability, never looked up repeatedly by ID.
has src/target/riscv64/core_admission.c 'semantic_calls->sites[call_site++]'
lacks src/target/riscv64/core_admission.c 'minic_core_call_index_get('
# Prepared RV32 and Direct LP64 CALL machines borrow the Common sorted
# semantic CALL index, leaving one canonical ordinal lookup for production.
has src/target/riscv32/rv32_abi_plan.h 'MinicCoreCallIndexHandle semantic_calls;'
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_index_find(calls,'
has src/target/common/direct_call_plan.h 'MinicCoreCallIndexHandle semantic_calls;'
has src/target/common/direct_call_plan.c 'minic_core_call_index_find(calls,'
lacks src/target/common/direct_instruction_semantics.h 'minic_multi_direct_result_slot_offset('
has src/target/common/direct_instruction_semantics.c 'static bool minic_multi_direct_result_slot_offset('
# RV64 capability admission and direct LP64 CALL preflight must also
# use the same Common semantic index in standalone and visitor modes.
has src/target/riscv64/core_admission.c 'minic_core_call_index_handle_prepare('
has src/target/riscv64/core_admission.c 'core_function_can_emit_with_index('
# Standalone APIs never own an extra Core CALL algorithm or binary search.
has src/target/common/core_call_index.c 'minic_core_call_index_handle_prepare('
has src/target/common/core_call_index.c 'minic_core_call_index_handle_destroy('
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_index_handle_prepare('
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_index_handle_destroy('
# Both Direct LP64 backends now admit and place ABI CALLs in a single
# semantic index traversal; do not reinstate the redundant old walker.
lacks Makefile 'src/target/common/direct_call_preflight.c'
lacks tests/compiler/bootstrap/b0-sources.txt 'src/target/common/direct_call_preflight.c'
has src/target/common/direct_call_plan.c 'static bool minic_lp64_admit_call('
has src/target/common/direct_call_plan.c 'types[j] = actual;'
lacks src/target/common/direct_call_plan.c 'minic_lp64_plan_direct_call('
has src/target/common/direct_function_plan.c 'failed_preflight ? "call_preflight" : "call_planning"'
has src/target/common/direct_call_plan.c 'minic_core_call_index_handle_prepare('
has src/target/common/direct_call_plan.c 'minic_core_call_index_handle_destroy('
lacks src/target/riscv32/rv32_abi_plan.c 'while (i < function->instruction_count'
lacks src/target/common/direct_call_plan.c 'while (id < fn->instruction_count'
# SysV VA cursors must match the prepared Common fact count exactly.
has src/target/x86_64/frame.c 'if (count != frame->varargs_count) return false;'
has src/target/common/direct_function_plan.c 'program, target, decl, fn, facts, abi, indirect_record,'
has src/target/common/direct_function_emit.h 'minic_multi_direct_lp64_function_plan_prepare('
lacks src/target/loongarch64/codegen.c 'minic_multi_direct_lp64_function_plan_prepare('
# All five targets must own only target callbacks. File lifetime,
# global/Core visit order and partial-output removal are ONE common transaction.
exists src/target/common/program_emit.h
has src/target/common/program_emit.h 'minic_program_visit_globals('
has src/target/common/program_emit.h 'minic_program_visit_prepared_functions('
has src/target/common/program_emit.h 'if (!ok) (void)remove(temporary);'
lacks src/target/common/program_emit.h 'out = fopen(path, "wb");'
has src/target/common/direct_program_emit.c 'minic_program_emit_write('
for backend in x86_64 riscv32 riscv64; do
  case "$backend" in
    x86_64) owner=src/target/x86_64/codegen.c ;;
    riscv32) owner=src/target/riscv32/rv32_emit.c ;;
    riscv64) owner=src/target/riscv64/codegen_function.c ;;
  esac
  has "$owner" 'minic_program_emit_write('
  lacks "$owner" 'fopen(path, "wb")'
  lacks "$owner" '(void)remove(path)'
done
# The only live definition filter is the common Core semantic predicate.
has src/target/common/program_visit.h 'decl->is_internal && !decl->is_referenced'
lacks src/target/x86_64/codegen.c 'for (i = 0U; i < program->function_count && success; ++i)'
lacks src/target/riscv64/codegen_function.c 'for (function_index = 0U;'
lacks src/target/riscv32/rv32_emit.c 'for (i = 0U; ok && i < function_count; ++i)'

# A native executable verifies success, five failure edges, cleanup and
# census continuation. Keep target GNU/QEMU checks in their own CI stages.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Only a finite x86 ISA grammar can feed raw GNU template substitution.
# A valid opcode prefix plus arbitrary trailing text is NOT a contract.
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -ffunction-sections \
  -fdata-sections -Wl,--gc-sections -Iinclude -Isrc \
  tests/compiler/c0/probe-x86-matched-asm-template.c \
  src/target/x86_64/asm_contract.c \
  -o "$work/x86-matched-asm-template-probe"
"$work/x86-matched-asm-template-probe"


# Unit test the borrow-or-own policy on the real linked Common implementation,
# including standalone objects, exact pointer sharing, malformed plans,
# and use-after-destroy prevention through zeroing the released alias.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-core-object-storage-ownership.c \
  src/target/common/core_object_storage.c \
  -o "$work/core-object-storage-ownership-probe"
"$work/core-object-storage-ownership-probe"

cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-program-emit-transaction.c \
  src/target/common/core_function_facts.c \
  src/target/common/core_function_plan.c \
  -o "$work/program-emit-probe"
"$work/program-emit-probe" "$work/test-output.s"

# Check the real shared CFG cursor on an all-block and RV32/RV64-style
# reachable subset before any ISA-dependent assembly emission.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-core-block-walk.c \
  -o "$work/core-block-walk-probe"
"$work/core-block-walk-probe"

# One host-native exercise of the real two-target Common machine dispatcher.
# Prove block and source-label order, barrier elision, GP writeback policy,
# and instruction/terminator failure ownership without emulated ISA binaries.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-direct-lp64-dispatch.c \
  -o "$work/direct-lp64-dispatch-probe"
"$work/direct-lp64-dispatch-probe"

# One-pass shared CALL/va_start semantic facts with zero-allocation
# standalone compatibility API, and an explicit owned-result lifecycle.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-core-facts-one-pass.c \
  src/target/common/core_function_facts.c \
  -o "$work/core-facts-single-pass-probe"
"$work/core-facts-single-pass-probe"

cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc \
  tests/compiler/c0/probe-core-instruction-family.c -o "$work/core-family-probe"
"$work/core-family-probe"

# The CoreCallIndex builder has its own true semantic probe below; the file
# emission/rollback harness deliberately uses synthetic CALL metadata.
# Verify ABI-neutral scalar/record/void/indirect CALL metadata and reject
# corrupt source Core values without any target toolchain or QEMU.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-core-call-semantics.c \
  src/target/common/core_call_view.c \
  src/target/common/core_call_index.c src/frontend/type.c \
  -o "$work/core-call-probe"
"$work/core-call-probe"

# Real RV32 ILP32 physical access safety. Function-section GC links only
# the production stack transport helpers, without a GNU cross toolchain.
# All malformed Core slots and scratch register aliases must fail closed.
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Iinclude -Isrc tests/compiler/c0/probe-rv32-frame-access.c \
  src/target/riscv32/rv32_frame.c \
  -o "$work/rv32-frame-access-probe"
"$work/rv32-frame-access-probe"

# Exercise the two historical SSA layouts in one true Common planner and
# prove bounds/rollback before testing native machine frame and QEMU ABIs.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-core-value-slots.c \
  src/target/common/core_value_slots.c src/frontend/type.c \
  -o "$work/core-value-slots-probe"
"$work/core-value-slots-probe"

# Prove checked alignment/reservation at overflow boundaries and no cursor
# mutation on failure before running slow QEMU/runtime architecture gates.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-core-frame-layout.c \
  -o "$work/frame-layout-probe"
"$work/frame-layout-probe"

printf 'MINIC_BACKEND_BOUNDARY_CHECK=PASS\n'
