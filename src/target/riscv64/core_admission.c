#include "target/riscv64/core_admission.h"
#include "target/common/core_instruction_family.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/function_abi_plan.h"
#include "target/riscv64/codegen_internal.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RV64 Core admission: full verification once, before machine output. */
/* M74_GLOBAL_RECORD_ADDRESS / M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER:
   RV64 global.addr must keep assembler-local `.L*` symbols PC-relative in
   early position-independent code. Use `lla` only for `.L*` symbols; keep
   global/extern symbols on `la`.  The pointee storage shape is irrelevant
   until a later memory operation, even for declaration-only void symbols. */
static bool core_global_addressable_type(MinicType type) {
    return minic_riscv64_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type) || minic_type_is_void(type);
}

static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    if (function == NULL || instruction == NULL) {
        return false;
    }
    /* A single Core semantic vocabulary for x86, RV32/RV64 and Direct
     * LP64. This only admits Core shapes into further target checks, NOT
     * a hardware opcode/extension (strict I remains target-owned). */
    if (minic_core_instruction_is_integer_scalar(instruction->kind) ||
        minic_core_instruction_is_floating_scalar(instruction->kind))
        return true;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return minic_riscv64_core_call_frame_address_supported(instruction);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return instruction->result < function->value_count &&
               instruction->value.operand < function->value_count &&
               minic_type_is_pointer(instruction->type) &&
               minic_type_equal(function->values[instruction->result].type, instruction->type) &&
               minic_type_equal(function->values[instruction->value.operand].type,
                                minic_type_unsigned_long());
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        /* RV64 atomic ISA/memory-order lowering is not qualified yet.
         * Never map Core atomic.store to an ordinary unqualified store. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        /* SysV 24-byte cursor copying is x86-only; RV64 keeps its linear
         * cursor-assignment path and must reject this opcode explicitly. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
        /* This typed SysV variadic read must never silently reuse the
         * existing RV64 8-byte linear va_arg lowering. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t fixed_stack_slots;
        size_t integer_parameter_count;

        return program != NULL && instruction->result < function->value_count &&
               minic_type_equal(function->values[instruction->result].type, instruction->type) &&
               minic_type_is_pointer(instruction->type) &&
               minic_riscv64_core_variadic_fixed_prefix(
                   program, function, &integer_parameter_count, &fixed_stack_slots);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
        return true;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return minic_riscv64_core_record_load_supported(program, function, instruction, NULL);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return minic_riscv64_core_record_copy_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = minic_riscv64_inline_asm_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        return binding != NULL && binding->register_name != NULL &&
               binding->register_name_length != 0U && minic_riscv64_scalar_type(binding->type) &&
               minic_type_equal(binding->type, instruction->type);
    }
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: RV64 spells the Core block label. */
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return minic_type_is_pointer(instruction->type) && instruction->value.block_id < function->block_count;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        return instruction->value.global_id < function->global_count &&
               function->globals[instruction->value.global_id].name != NULL &&
               function->globals[instruction->value.global_id].name_length != 0U;
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
        MinicType function_type;
        MinicCoreFunctionSymbolId symbol_id = instruction->value.function_symbol_id;
        return symbol_id < function->function_symbol_count &&
               function->function_symbols[symbol_id].name != NULL &&
               function->function_symbols[symbol_id].name_length != 0U &&
               minic_type_pointee(instruction->type, &function_type) &&
               minic_type_is_function(function_type);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;
        size_t result_size;
        bool is_unsigned;

        return minic_riscv64_core_integer_overflow_supported(
            program, function, instruction, &result_type, &result_size, &is_unsigned);
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return minic_riscv64_inline_asm_instruction_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return minic_riscv64_core_direct_call_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return minic_riscv64_core_indirect_call_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return minic_riscv64_core_field_address_supported(instruction, NULL);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return minic_riscv64_core_scalar_bitcast_supported(program, function, instruction);
    default:
        return false;
    }
    return false;
}

static bool core_rv64_capability_reject(const MinicCoreFunction *function,
                                        const char *stage,
                                        size_t index,
                                        int instruction_kind) {
    const char *trace;

    trace = getenv("CORE_FAST_TRACE");
    if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
        fprintf(stderr,
                "CORE_RV64_CAP function=%.*s stage=%s index=%zu instruction_kind=%d\n",
                function == NULL ? 0 : (int)function->name_length,
                function == NULL || function->name == NULL ? "" : function->name,
                stage == NULL ? "unknown" : stage,
                index,
                instruction_kind);
    }
    return false;
}

static bool core_function_can_emit(const MinicC0Program *program,
                                   const MinicCoreFunction *function,
                                   const MinicRiscv64FunctionAbiPlan *abi_plan) {
    size_t index;

    if (function == NULL || !minic_core_function_verify(function)) {
        return core_rv64_capability_reject(function, "verify", 0U, -1);
    }
    if (program == NULL) {
        if (!minic_type_is_void(function->return_type) &&
            !minic_riscv64_scalar_type(function->return_type)) {
            return core_rv64_capability_reject(function, "legacy-return", 0U, -1);
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            if (!minic_riscv64_scalar_type(function->parameter_types[index])) {
                return core_rv64_capability_reject(function, "legacy-parameter", index, -1);
            }
        }
    } else {
        MinicRiscv64AbiValue return_value;
        if (abi_plan == NULL || !abi_plan->prepared ||
            abi_plan->parameter_location_count != function->parameter_count) {
            return core_rv64_capability_reject(function, "function-abi", 0U, -1);
        }
        return_value = abi_plan->return_value;
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INVALID) {
            return core_rv64_capability_reject(function, "return-abi", 0U, -1);
        }
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            if (return_value.slot_count == 0U || return_value.slot_count > 2U) {
                return core_rv64_capability_reject(function, "return-aggregate", 0U, -1);
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U) {
                return core_rv64_capability_reject(function, "return-indirect", 0U, -1);
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
            if ((!minic_type_is_long_double(function->return_type) &&
                 !minic_type_is_int128_integer(function->return_type)) ||
                return_value.storage_size != 16U || return_value.slot_count != 2U) {
                return core_rv64_capability_reject(
                    function, "return-wide-scalar", 0U, -1);
            }
        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                   !(return_value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                     (minic_type_is_float(function->return_type) ||
                      minic_type_is_double(function->return_type)))) {
            return core_rv64_capability_reject(function, "return-kind", 0U, -1);
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            const MinicRiscv64AbiArgumentLocation location =
                abi_plan->parameter_locations[index];
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (!minic_type_is_record(function->parameter_types[index]) ||
                    location.value.storage_size == 0U || location.value.slot_count != 1U ||
                    location.floating_register_count != 0U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return core_rv64_capability_reject(function, "parameter-indirect", index, -1);
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT) {
                bool soft = abi_plan->named_parameter_cursor.lp64_soft_float;
                bool correct_location =
                    soft ? (location.floating_register_count == 0U &&
                            location.integer_register_count +
                              location.stack_slot_count == 1U)
                         : (location.floating_register_count == 1U &&
                            location.floating_register_begin < 8U &&
                            location.integer_register_count == 0U &&
                            location.stack_slot_count == 0U);
                if ((!minic_type_is_float(function->parameter_types[index]) &&
                     !minic_type_is_double(function->parameter_types[index])) ||
                    !correct_location) {
                    return core_rv64_capability_reject(function, "parameter-float", index, -1);
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
                if ((!minic_type_is_long_double(function->parameter_types[index]) &&
                     !minic_type_is_int128_integer(function->parameter_types[index])) ||
                    location.value.storage_size != 16U || location.value.slot_count != 2U ||
                    location.floating_register_count != 0U ||
                    location.integer_register_count + location.stack_slot_count != 2U ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return core_rv64_capability_reject(
                        function, "parameter-wide-scalar", index, -1);
                }
            } else if (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                       location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                       (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                        location.value.slot_count == 0U || location.value.slot_count > 2U)) {
                return core_rv64_capability_reject(function, "parameter-kind", index, -1);
            }
        }
    }
    for (index = 0U; index < function->object_count; ++index) {
        size_t object_size;
        size_t object_alignment;
        MinicType object_type;

        object_type = function->objects[index].type;
        if ((!minic_riscv64_scalar_type(object_type) && !minic_type_is_record(object_type) &&
             !minic_type_is_array(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U ||
            (function->objects[index].explicit_alignment != 0U &&
             (function->objects[index].explicit_alignment &
              (function->objects[index].explicit_alignment - 1U)) != 0U)) {
            return core_rv64_capability_reject(function, "object", index, -1);
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||
            !core_global_addressable_type(function->globals[index].type)) {
            return core_rv64_capability_reject(function, "global", index, -1);
        }
    }
    for (index = 0U; index < function->value_count; ++index) {
        if (!minic_riscv64_scalar_type(function->values[index].type)) {
            return core_rv64_capability_reject(function, "value", index, -1);
        }
    }
    for (index = 0U; index < function->instruction_count; ++index) {
        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            return core_rv64_capability_reject(
                function, "instruction", index, (int)function->instructions[index].kind);
        }
    }
    return true;
}

bool minic_riscv64_core_function_admit_profile(
    const MinicC0Program *program, const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan, bool lp64_soft_float) {
    if (function == NULL || !minic_core_function_verify(function))
        return core_rv64_capability_reject(function, "verify", 0U, -1);
    if (program != NULL &&
        (abi_plan == NULL ||
         !minic_riscv64_function_abi_plan_build_profile(
             program, function, abi_plan, lp64_soft_float)))
        return core_rv64_capability_reject(function, "function-abi", 0U, -1);
    return core_function_can_emit(program, function, abi_plan);
}
bool minic_riscv64_core_function_admit(
    const MinicC0Program *program, const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan) {
    return minic_riscv64_core_function_admit_profile(
        program, function, abi_plan, false);
}
bool minic_riscv64_core_function_can_emit(const MinicCoreFunction *function) {
    return core_function_can_emit(NULL, function, NULL);
}
bool minic_riscv64_core_function_can_emit_for_program(
    const MinicC0Program *program, const MinicCoreFunction *function) {
    MinicRiscv64FunctionAbiPlan plan = {0};
    bool ok;
    if (program == NULL) return false;
    ok = minic_riscv64_core_function_admit(program, function, &plan);
    minic_riscv64_function_abi_plan_destroy(&plan);
    return ok;
}

