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
has src/target/riscv32/rv32_integer_emit.c 'minic_riscv_integer_arithmetic_symbol('
has src/target/riscv32/rv32_integer_emit.c 'rv32_emit_wide_integer('
has src/target/riscv32/rv32_wide_emit.c 'minic_riscv_integer_arithmetic_symbol('
lacks src/target/riscv32/rv32_integer_emit.c '  add t4, t0, t2'
has src/target/riscv32/rv32_fp_emit.c 'minic_riscv_softfloat_arithmetic_symbol('
has src/target/riscv32/rv32_memory_emit.c 'minic_riscv_emit_record_copy_bytes('
has src/target/riscv32/rv32_call.c 'rv32_emit_parameter('
has src/target/riscv32/rv32_abi_plan.c 'minic_core_call_view_build('
has src/target/riscv32/rv32_abi_plan.c 'minic_riscv32_abi_place('
has src/target/riscv32/rv32_frame.c 'rv32_function_abi_plan_build('
has src/target/riscv32/rv32_frame.c 'varargs_offset'
has src/target/riscv32/rv32_emit.c 'MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS'
has src/target/target_info.c 'rv32_target.variadic_linear_slot_bytes = 4U'
lacks src/target/riscv32/rv32_emit.c 'defining variadic functions requires ILP32 va_list ABI'
has src/target/riscv32/rv32_frame.c 'max_outgoing_stack_words'
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
