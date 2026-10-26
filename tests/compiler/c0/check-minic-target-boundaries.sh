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

has src/target/common/direct_instruction_semantics.c \
  'minic_multi_direct_machine_instruction_shape('
has src/target/common/direct_residual_plan.c \
  'minic_multi_direct_integer_cast_plan('
has src/target/x86_64/machine_call.c \
  'minic_x86_64_plan_outgoing_arguments('
has src/target/riscv64/core_codegen.c \
  'minic_riscv64_core_function_admit_profile('
has src/target/riscv64/frame_layout.c \
  'minic_core_object_reuse_plan_build('

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

for arch in aarch64 loongarch64; do
  block="src/target/$arch/block_emit.c"
  residual="src/target/$arch/residual_emit.c"
  header="src/target/$arch/residual_emit.h"
  for file in "$block" "$residual" "$header"; do exists "$file"; done
  has "$block" 'minic_multi_direct_machine_instruction_shape('
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
has tools/minic-cc/main.c 'target/riscv64/isa.h'
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
has src/target/riscv64/isa.c 'minic_riscv_profile_find(march)'
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
has src/target/riscv32/rv32_frame.c 'minic_core_block_reused_spill_layout('
has src/target/riscv32/rv32_frame.c 'minic_core_object_reuse_plan_build('
exists src/target/common/core_object_reuse.c
exists src/target/common/core_object_intervals.c
has src/target/riscv64/frame_layout.c 'minic_core_object_reuse_plan_build('
has src/target/riscv64/frame_layout.c 'minic_core_block_reused_spill_layout('
exists src/target/common/core_spill_layout.c
exists src/target/common/core_spill_layout.h
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
has src/target/riscv32/rv32_emit.c 'minic_core_instruction_is_integer_scalar(ins->kind)'
has src/target/riscv64/core_admission.c 'minic_core_instruction_is_integer_scalar(instruction->kind)'
has src/target/riscv64/core_codegen.c 'minic_core_instruction_is_floating_scalar(instruction->kind)'
has src/target/common/direct_instruction_semantics.c 'minic_core_instruction_is_floating_scalar(kind)'
has src/target/common/direct_call_preflight.c 'minic_core_instruction_is_call(ins->kind)'
has src/target/common/program_visit.h 'minic_program_visit_globals('
has src/target/common/program_visit.h 'minic_program_visit_functions('
has src/target/common/program_visit.h 'minic_program_visit_prepared_functions('
# All five machine backends must borrow one visitor-owned per-function Core
# object plan, not repeat layout scans. Each target keeps its own psABI frame.
has src/target/common/core_function_plan.h 'minic_core_object_storage_plan_build('
has src/target/common/core_function_plan.h 'minic_core_object_storage_plan_destroy(&plan->objects)'
has src/target/x86_64/codegen.c 'minic_x86_64_frame_init_with_storage('
has src/target/riscv32/rv32_emit.c 'rv32_frame_init_with_storage('
has src/target/riscv64/codegen_function.c 'minic_riscv64_emit_core_function_for_program_with_symbol_with_storage('
has src/target/aarch64/codegen.c 'fn, facts, storage, "aarch64"'
has src/target/loongarch64/codegen.c 'fn, facts, storage, "loongarch64"'
has src/target/x86_64/frame.c 'frame->borrows_object_storage = true'
has src/target/riscv32/rv32_frame.c 'frame->borrows_object_storage = true'
has src/target/riscv64/frame_layout.c 'frame->borrows_object_storage = true'
has src/target/common/lp64_frame.c 'plan->borrows_object_storage = true'

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
has src/target/common/lp64_frame.c 'minic_core_object_storage_plan_build('
has src/target/riscv32/rv32_frame.c 'minic_core_object_storage_plan_build('
has src/target/riscv64/frame_layout.c 'minic_core_object_storage_plan_build('
has src/target/x86_64/frame.c 'minic_core_object_storage_plan_build('
has src/target/riscv32/rv32_frame.c 'minic_core_frame_reserve('
has src/target/common/lp64_frame.c 'minic_core_frame_reserve('
has src/target/riscv64/frame_layout.c 'minic_core_frame_align_up('
has src/target/x86_64/frame.c 'minic_core_frame_align_up('
lacks src/target/riscv32/rv32_frame.c 'minic_core_object_storage(program,'
lacks src/target/common/lp64_frame.c 'minic_core_object_storage(program,'

# The semantic CALL resolver is the single Core identity owner. psABI
# GP/FP/stack placement remains x86/RV32/RV64/AArch64/LoongArch-owned.
# Five targets get one immutable CallIndex with visitor-managed lifetime.
exists src/target/common/core_call_index.c
has src/target/common/core_function_plan.h 'minic_core_call_index_build('
has src/target/common/core_function_plan.h 'minic_core_call_index_destroy(&plan->calls)'
has src/target/common/program_visit.h 'minic_core_function_plan_build('
has src/target/common/program_visit.h 'minic_core_function_plan_destroy(&plan)'
has src/target/riscv32/rv32_frame.c 'facts->call_index'
has src/target/riscv64/frame_layout.c 'facts->call_index'
has src/target/common/direct_function_plan.c 'facts->call_index'
has src/target/common/direct_call_preflight.c 'minic_core_call_index_get(semantic_calls, i)'
has src/target/common/core_call_view.c 'view.return_form = MINIC_CORE_CALL_RETURN_OBJECT'
has src/target/common/core_call_view.c 'minic_core_call_view_argument_type(function, &view, i, &actual)'
has src/target/x86_64/call_plan.c 'minic_core_call_view_argument_type(fn, view, i, &type)'
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_view_argument_type('
has src/target/common/direct_call_plan.c 'minic_core_call_view_argument_type(fn, &view, j, &types[j])'
has src/target/riscv64/call_emit.c 'call.return_form != MINIC_CORE_CALL_RETURN_OBJECT'
has src/target/common/core_function_facts.c 'facts->max_call_arity = arity'
has src/target/common/core_function_plan.h 'minic_core_function_facts_analyze('
lacks src/target/common/lp64_frame.c 'bool minic_lp64_frame_plan_objects('
lacks src/target/riscv64/frame_layout.c 'bool minic_riscv64_core_frame_initialize_prepared('
lacks src/target/x86_64/frame.c 'minic_x86_64_frame_init_prepared('
lacks src/target/riscv32/rv32_frame.c 'rv32_frame_init_prepared('
has src/target/riscv64/core_codegen.c 'minic_riscv64_emit_core_function_for_program_with_symbol_prepared('
has src/target/riscv64/frame_layout.c 'facts->needs_saved_return_address'
has src/target/riscv64/frame_layout.c 'facts->uses_structured_inline_asm'
has src/target/riscv32/rv32_frame.c 'facts->uses_dynamic_stack_alloc'
has src/target/x86_64/frame.c 'facts->variadic_address_count'
has src/target/common/direct_function_profile.c 'facts->variadic_address_count'
has src/target/common/direct_function_profile.c 'facts->uses_dynamic_stack_alloc'
has src/target/common/direct_function_plan.c 'program, target, decl, fn, facts, abi, indirect_record,'
has src/target/aarch64/codegen.c 'minic_multi_direct_lp64_function_plan_prepare('
has src/target/loongarch64/codegen.c 'minic_multi_direct_lp64_function_plan_prepare('
# All five targets must own only target callbacks. File lifetime,
# global/Core visit order and partial-output removal are ONE common transaction.
exists src/target/common/program_emit.h
has src/target/common/program_emit.h 'minic_program_visit_globals('
has src/target/common/program_emit.h 'minic_program_visit_prepared_functions('
has src/target/common/program_emit.h 'if (!ok) (void)remove(path);'
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
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-program-emit-transaction.c \
  src/target/common/core_function_facts.c \
  -o "$work/program-emit-probe"
"$work/program-emit-probe" "$work/test-output.s"

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

# Prove checked alignment/reservation at overflow boundaries and no cursor
# mutation on failure before running slow QEMU/runtime architecture gates.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude -Isrc tests/compiler/c0/probe-core-frame-layout.c \
  -o "$work/frame-layout-probe"
"$work/frame-layout-probe"

printf 'MINIC_BACKEND_BOUNDARY_CHECK=PASS\n'
