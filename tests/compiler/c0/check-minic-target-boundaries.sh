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
  src/target/riscv64/function_abi_plan.c \
  src/target/riscv64/object_intervals.c \
  src/target/riscv64/object_liveness.c; do
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
  'minic_riscv64_object_reuse_plan_build('

lacks src/target/riscv64/core_codegen.c 'core_instruction_supported('
lacks src/target/riscv64/core_codegen.c \
  'minic_riscv64_object_liveness_begin_function('
lacks src/target/riscv64/object_liveness.c \
  'core_scalar_object_reuse_cache'
lacks src/target/riscv64/object_intervals.c \
  'static CoreObjectAddressUse *core_object_address_use_index'
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
has src/target/riscv64/object_liveness.c 'minic_core_object_reuse_plan_build('
has src/target/riscv64/spill_layout.c 'minic_core_block_reused_spill_layout('
exists src/target/common/core_spill_layout.c
exists src/target/common/core_spill_layout.h
has src/target/riscv32/rv32_frame.c 'varargs_offset'
has src/target/riscv32/rv32_emit.c 'MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS'
has src/target/target_info.c 'rv32_target.variadic_linear_slot_bytes = 4U'
lacks src/target/riscv32/rv32_emit.c 'defining variadic functions requires ILP32 va_list ABI'
has src/target/riscv32/rv32_frame.c 'max_outgoing_stack_words'
has src/target/riscv32/rv32_frame.c 'minic_core_function_facts_analyze('
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

printf 'MINIC_BACKEND_BOUNDARY_CHECK=PASS\n'
