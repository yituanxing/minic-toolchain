#include "target/riscv64/core_codegen.h"

#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/call_emit.h"
#include "target/riscv64/asm_plan.h"
#include "target/riscv64/frame_layout.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/scalar_emit.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/spill_layout.h"
#include "target/riscv64/object_liveness.h"
#include "target/riscv64/legalize.h"
#include "core/value_uses.h"
#include "target/data_layout.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const MinicCoreFixedRegisterBinding *core_fixed_register_binding(
    const MinicCoreFunction *function, size_t binding_id) {
    return function != NULL && binding_id < function->fixed_register_binding_count
               ? &function->fixed_register_bindings[binding_id]
               : NULL;
}

/* M74_GLOBAL_RECORD_ADDRESS / M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER:
   RV64 global.addr must keep assembler-local `.L*` symbols PC-relative in
   early position-independent code. Use `lla` only for `.L*` symbols; keep
   global/extern symbols on `la`.  The pointee storage shape is irrelevant
   until a later memory operation, even for declaration-only void symbols. */
static bool core_global_addressable_type(MinicType type) {
    return minic_riscv64_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type) || minic_type_is_void(type);
}

static bool core_frame_fail(MinicRiscv64CoreFrame *frame) {
    minic_riscv64_core_frame_destroy(frame);
    return false;
}

static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,
                                             const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: opaque volatile asm may carry zero
       target bytes; emit_opaque_inline_asm naturally loops zero times. */
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile) {
        return false;
    }
    /* M76_SINGLE_LABEL_ASM_GOTO: the target is explicit Core metadata even
       though the target-specific template remains opaque. */
    return !inline_asm->is_goto || inline_asm->goto_target < function->block_count;
}

static bool core_register_output_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM ||
        (!minic_type_is_integer(instruction->type) && !minic_type_is_pointer(instruction->type)) ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_register_output_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId operand;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM ||
        (!minic_type_is_integer(instruction->type) && !minic_type_is_pointer(instruction->type)) ||
        instruction->value.register_output_input_inline_asm.inline_asm_id >=
            function->inline_asm_count) {
        return false;
    }
    operand = instruction->value.register_output_input_inline_asm.operand;
    if (operand >= function->value_count ||
        (!minic_type_is_integer(function->values[operand].type) &&
         !minic_type_is_pointer(function->values[operand].type))) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.register_output_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0' &&
             inline_asm->template_text[index + 1U] != '1')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_memory_readwrite_scalar_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId memory_address;
    MinicCoreValueId operand;
    size_t memory_index;
    size_t register_index;
    size_t scalar_index;
    size_t index;
    bool has_register_output;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM ||
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id >=
            function->inline_asm_count) {
        return false;
    }
    memory_address = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address;
    operand = instruction->value.memory_readwrite_scalar_input_inline_asm.operand;
    memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
    register_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
    scalar_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
    has_register_output = register_index != SIZE_MAX;
    if (memory_address >= function->value_count || operand >= function->value_count ||
        !minic_type_is_pointer(function->values[memory_address].type) ||
        (!minic_type_is_integer(function->values[operand].type) &&
         !minic_type_is_pointer(function->values[operand].type)) ||
        memory_index > 9U || scalar_index > 9U || memory_index == scalar_index ||
        (has_register_output &&
         (register_index > 9U || register_index == memory_index || register_index == scalar_index))) {
        return false;
    }
    if (has_register_output) {
        if ((!minic_type_is_integer(instruction->type) &&
             !minic_type_is_pointer(instruction->type)) ||
            instruction->result == MINIC_CORE_VALUE_INVALID) {
            return false;
        }
    } else if (!minic_type_is_void(instruction->type) ||
               instruction->result != MINIC_CORE_VALUE_INVALID) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile || !inline_asm->has_memory_clobber) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        unsigned char ch;

        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index + 1U];
        if (ch == '%') {
            index += 1U;
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_index != memory_index && operand_index != scalar_index &&
            (!has_register_output || operand_index != register_index)) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_scalar_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId operand;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(instruction->type) ||
        instruction->value.scalar_input_inline_asm.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    operand = instruction->value.scalar_input_inline_asm.operand;
    if (operand >= function->value_count ||
        (!minic_type_is_integer(function->values[operand].type) &&
         !minic_type_is_pointer(function->values[operand].type))) {
        return false;
    }
    inline_asm =
        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

/* M126A_GENERIC_STRUCTURED_ASM: capability is now role/resource based. */
static bool core_structured_inline_asm_supported(const MinicCoreFunction *function,
                                                 const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicRiscv64AsmInstructionPlan probe;
    bool bound[10] = {false};
    size_t binding_index;
    size_t template_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_void(instruction->type) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count == 0U ||
        instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile || inline_asm->is_goto) {
        return false;
    }
    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const MinicCoreFixedRegisterBinding *fixed_binding = NULL;
        MinicType pointee;
        MinicType value_type;

        if (binding->operand_index > 9U || bound[binding->operand_index] ||
            binding->value >= function->value_count ||
            (binding->early_clobber &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {
            return false;
        }
        bound[binding->operand_index] = true;
        if (binding->has_fixed_register_binding) {
            fixed_binding =
                core_fixed_register_binding(function, binding->fixed_register_binding_id);
            if (fixed_binding == NULL || !fixed_binding->is_local) {
                return false;
            }
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !minic_riscv64_scalar_type(value_type) ||
                (fixed_binding != NULL && !minic_type_equal(fixed_binding->type, value_type))) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (fixed_binding != NULL ||
                !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !minic_riscv64_scalar_type(value_type)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!minic_riscv64_scalar_type(function->values[binding->value].type) ||
                (fixed_binding != NULL &&
                 !minic_type_equal(fixed_binding->type, function->values[binding->value].type))) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    for (template_index = 0U; template_index < inline_asm->template_length; ++template_index) {
        unsigned char ch;
        if (inline_asm->template_text[template_index] != '%') {
            continue;
        }
        if (++template_index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[template_index];
        if (ch == '%') {
            continue;
        }
        if (ch == 'z') {
            if (++template_index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[template_index];
        }
        if (ch < '0' || ch > '9' || !bound[(size_t)(ch - '0')]) {
            return false;
        }
    }
    return minic_riscv64_asm_instruction_plan_build(function, instruction, &probe);
}

/* M85_RECORD_CALL_ARGUMENT: validate direct-call arguments against the
   shared RV64 ABI classifier. Core stores VALUE/OBJECT form only; register
   placement remains entirely target-owned. The first caller tier intentionally
   stays register-only, matching the pre-M85 Core call backend's no-stack scope. */
static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    if (function == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return true;
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

        binding = core_fixed_register_binding(
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
        return core_opaque_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        return core_register_output_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return core_register_output_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return core_memory_readwrite_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return core_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return core_structured_inline_asm_supported(function, instruction);
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
                                   const MinicCoreFunction *function) {
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
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value)) {
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
            if (!minic_type_is_long_double(function->return_type) ||
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
            MinicRiscv64AbiArgumentLocation location;

            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location)) {
                return core_rv64_capability_reject(function, "parameter-abi", index, -1);
            }
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
                if ((!minic_type_is_float(function->parameter_types[index]) &&
                     !minic_type_is_double(function->parameter_types[index])) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return core_rv64_capability_reject(function, "parameter-float", index, -1);
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
                if (!minic_type_is_long_double(function->parameter_types[index]) ||
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

bool minic_riscv64_core_function_can_emit(const MinicCoreFunction *function) {
    return core_function_can_emit(NULL, function);
}

bool minic_riscv64_core_function_can_emit_for_program(const MinicC0Program *program,
                                                               const MinicCoreFunction *function) {
    return program != NULL && core_function_can_emit(program, function);
}

static bool emit_field_address(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    size_t field_offset;

    if (!minic_riscv64_core_field_address_supported(instruction, &field_offset) ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.field_address.base, "t0")) {
        return false;
    }
    if (field_offset != 0U) {
        if (field_offset <= 2047U) {
            if (fprintf(file, "  addi t0, t0, %zu\n", field_offset) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t1, %zu\n"
                           "  add t0, t0, t1\n",
                           field_offset) < 0) {
            return false;
        }
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicCoreFunction *function,
                                   const char *symbol_name,
                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || !core_opaque_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->is_goto) {
        bool deferred_immediate = inline_asm->source_inline_asm_id != SIZE_MAX;

        if (symbol_name == NULL || symbol_name[0] == '\0' ||
            inline_asm->goto_target >= function->block_count) {
            return false;
        }
        if (deferred_immediate) {
            if (fprintf(file,
                        "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                        "  .extern __minic_deferred_asm_immediate_%zu_0\n"
                        "  ",
                        inline_asm->source_inline_asm_id) < 0) {
                return false;
            }
        } else if (fprintf(file, "  ") < 0) {
            return false;
        }
        for (index = 0U; index < inline_asm->template_length; ++index) {
            if (inline_asm->template_text[index] != '%') {
                if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                    return false;
                }
                continue;
            }
            if (index + 1U >= inline_asm->template_length) {
                return false;
            }
            if (inline_asm->template_text[index + 1U] == '%') {
                if (fputc('%', file) == EOF) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (inline_asm->template_text[index + 1U] == '0') {
                if (!deferred_immediate ||
                    fprintf(file,
                            "__minic_deferred_asm_immediate_%zu_0",
                            inline_asm->source_inline_asm_id) < 0) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (index + 2U < inline_asm->template_length &&
                inline_asm->template_text[index + 1U] == 'l' &&
                inline_asm->template_text[index + 2U] == '[') {
                size_t name_end = index + 3U;
                while (name_end < inline_asm->template_length &&
                       inline_asm->template_text[name_end] != ']') {
                    name_end += 1U;
                }
                if (name_end >= inline_asm->template_length || name_end == index + 3U ||
                    fprintf(file,
                            ".L%s_core_bb%" PRIu32,
                            symbol_name,
                            inline_asm->goto_target) < 0) {
                    return false;
                }
                index = name_end;
                continue;
            }
            return false;
        }
        return fputc('\n', file) != EOF;
    }
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            inline_asm->template_text[index + 1U] != '%') {
            return false;
        }
        if (fputc('%', file) == EOF) {
            return false;
        }
        index += 1U;
    }
    return fputc('\n', file) != EOF;
}

static bool emit_register_output_inline_asm(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_register_output_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        (minic_type_is_integer(instruction->type) &&
         !minic_riscv64_emit_integer_conversion_for_program(
             file, program, instruction->type, "t0"))) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

static bool emit_register_output_input_inline_asm(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_register_output_input_inline_asm_supported(function, instruction) ||
        !minic_riscv64_frame_load_value(
            file, frame, instruction->value.register_output_input_inline_asm.operand, "t1")) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.register_output_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '1') {
            if (fprintf(file, "t1") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        (minic_type_is_integer(instruction->type) &&
         !minic_riscv64_emit_integer_conversion_for_program(
             file, program, instruction->type, "t0"))) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

static bool emit_memory_readwrite_scalar_input_inline_asm(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t memory_index;
    size_t register_index;
    size_t scalar_index;
    size_t index;
    bool has_register_output;

    if (file == NULL || program == NULL || frame == NULL ||
        !core_memory_readwrite_scalar_input_inline_asm_supported(function, instruction) ||
        !minic_riscv64_frame_load_value(
            file,
            frame,
            instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
            "t0") ||
        !minic_riscv64_frame_load_value(
            file, frame, instruction->value.memory_readwrite_scalar_input_inline_asm.operand, "t2")) {
        return false;
    }
    memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
    register_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
    scalar_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
    has_register_output = register_index != SIZE_MAX;
    inline_asm = &function->inline_asms[
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        unsigned char ch;

        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_index == memory_index) {
            if (fprintf(file, "(t0)") < 0) {
                return false;
            }
        } else if (has_register_output && operand_index == register_index) {
            if (fprintf(file, "t1") < 0) {
                return false;
            }
        } else if (operand_index == scalar_index) {
            if (fprintf(file, "t2") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF) {
        return false;
    }
    if (!has_register_output) {
        return true;
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "t1")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
}

static bool emit_scalar_input_inline_asm(
    FILE *file,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_scalar_input_inline_asm_supported(function, instruction) ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.scalar_input_inline_asm.operand, "t0")) {
        return false;
    }
    inline_asm =
        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    return fputc('\n', file) != EOF;
}

static bool emit_structured_inline_asm(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicRiscv64CoreFrame *frame,
                                       const MinicCoreInstruction *instruction,
                                       size_t instruction_id) {
    const MinicCoreInlineAsm *inline_asm;
    const MinicRiscv64AsmInstructionPlan *plan;
    const char *const *operand_registers;
    const bool *memory_operand;
    const char *scratch_register;
    size_t binding_index;
    size_t index;

    if (file == NULL || program == NULL || frame == NULL || function == NULL ||
        instruction == NULL ||
        (plan = minic_riscv64_asm_function_plan_get(&frame->asm_plan, instruction_id)) == NULL)
        return false;
    operand_registers = plan->operand_registers;
    memory_operand = plan->memory_operand;
    scratch_register = plan->scratch_register;
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name = operand_registers[binding->operand_index];
        MinicType pointee;
        MinicType value_type;

        if (register_name == NULL) {
            return false;
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_riscv64_frame_load_value(file, frame, binding->value, scratch_register) ||
                !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) ||
                !minic_riscv64_emit_scalar_load_for_program(
                    file, program, value_type, register_name, scratch_register)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!minic_riscv64_frame_load_value(file, frame, binding->value, register_name)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;
        size_t operand_index;
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (++index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch == 'z') {
            if (++index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[index];
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_registers[operand_index] == NULL) {
            return false;
        }
        if (memory_operand[operand_index]) {
            if (fprintf(file, "(%s)", operand_registers[operand_index]) < 0) {
                return false;
            }
        } else if (fprintf(file, "%s", operand_registers[operand_index]) < 0) {
            return false;
        }
    }
    if (fputc('\n', file) == EOF) {
        return false;
    }

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;
        MinicType pointee;
        MinicType value_type;

        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            continue;
        }
        register_name = operand_registers[binding->operand_index];
        if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
            !minic_type_unqualified(pointee, &value_type) ||
            (minic_type_is_integer(value_type) &&
             !minic_riscv64_emit_integer_conversion_for_program(
                 file, program, value_type, register_name)) ||
            !minic_riscv64_frame_load_value(file, frame, binding->value, scratch_register) ||
            !minic_riscv64_emit_scalar_store_for_program(
                file, program, value_type, register_name, scratch_register)) {
            return false;
        }
    }
    return true;
}

static bool emit_instruction(FILE *file,
                             const MinicC0Program *program,
                             const MinicCoreFunction *function,
                             const MinicRiscv64CoreFrame *frame,
                             const char *symbol_name,
                             const MinicCoreInstruction *instruction,
                             size_t instruction_id) {
    size_t object_offset;

    if (file == NULL || function == NULL || frame == NULL || symbol_name == NULL || instruction == NULL ||
        !core_instruction_supported(program, function, instruction)) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        return false;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return minic_riscv64_emit_scalar_instruction(
            file, program, function, frame, symbol_name, instruction);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.pointer_offset.base, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.pointer_offset.index, "t1")) {
            return false;
        }
        if (instruction->value.pointer_offset.element_size != 1U &&
            fprintf(file,
                    "  li t2, %zu\n"
                    "  mul t1, t1, t2\n",
                    instruction->value.pointer_offset.element_size) < 0) {
            return false;
        }
        /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: retain subtraction through
           the Core boundary so an unsigned index is scaled before `sub`. */
        if (fprintf(file,
                    "  %s t0, t0, t1\n",
                    instruction->value.pointer_offset.subtract ? "sub" : "add") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = core_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U || !minic_riscv64_scalar_type(binding->type) ||
            !minic_type_equal(binding->type, instruction->type) ||
            fprintf(file, "  mv t0, %s\n", binding->register_name) < 0) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        if (!minic_riscv64_core_call_frame_address_supported(instruction)) {
            return false;
        }
        if (instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN) {
            if (!frame->saves_return_address ||
                !minic_riscv64_frame_emit_load64(file, frame, "t0", frame->return_address_offset)) {
                return false;
            }
        } else if (instruction->value.call_frame_address.kind ==
                   MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) {
            if (fprintf(file,
                        frame->has_dynamic_stack_alloc ? "  mv t0, s0\n" : "  mv t0, sp\n") < 0) {
                return false;
            }
        } else {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        if (!frame->has_dynamic_stack_alloc ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  addi t0, t0, 15\n"
                    "  li t1, -16\n"
                    "  and t0, t0, t1\n"
                    "  sub sp, sp, t0\n") < 0) {
            return false;
        }
        if (frame->dynamic_call_area_size == 0U) {
            if (fprintf(file, "  mv t0, sp\n") < 0) {
                return false;
            }
        } else if (frame->dynamic_call_area_size <= 2047U) {
            if (fprintf(file,
                        "  addi t0, sp, %zu\n",
                        frame->dynamic_call_area_size) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t0, %zu\n"
                           "  add t0, sp, t0\n",
                           frame->dynamic_call_area_size) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        if (!frame->has_variadic_argument_address) {
            return false;
        }
        if (frame->varargs_size != 0U) {
            if (!minic_riscv64_frame_emit_address(file, frame, "t0", frame->varargs_offset)) {
                return false;
            }
        } else {
            size_t stack_byte_offset;

            if (frame->variadic_fixed_stack_slots > SIZE_MAX / 8U) {
                return false;
            }
            stack_byte_offset = frame->variadic_fixed_stack_slots * 8U;
            if (frame->has_dynamic_stack_alignment) {
                if (!minic_riscv64_frame_emit_load64(file, frame, "t0", frame->entry_sp_offset)) {
                    return false;
                }
            } else {
                if (frame->frame_size <= 2047U) {
                    if (fprintf(file,
                                frame->has_dynamic_stack_alloc
                                    ? "  addi t0, s0, %zu\n"
                                    : "  addi t0, sp, %zu\n",
                                frame->frame_size) < 0) {
                        return false;
                    }
                } else if (fprintf(file,
                                   frame->has_dynamic_stack_alloc
                                       ? "  li t0, %zu\n  add t0, s0, t0\n"
                                       : "  li t0, %zu\n  add t0, sp, t0\n",
                                   frame->frame_size) < 0) {
                    return false;
                }
            }
            if (stack_byte_offset != 0U) {
                if (stack_byte_offset <= 2047U) {
                    if (fprintf(file, "  addi t0, t0, %zu\n", stack_byte_offset) < 0) {
                        return false;
                    }
                } else if (fprintf(file,
                                   "  li t1, %zu\n"
                                   "  add t0, t0, t1\n",
                                   stack_byte_offset) < 0) {
                    return false;
                }
            }
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return minic_riscv64_emit_incoming_parameter(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return minic_riscv64_emit_incoming_parameter_object(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!minic_riscv64_core_frame_object_offset(program, function, frame, instruction->value.object_id, &object_offset) ||
            !minic_riscv64_frame_emit_address(file, frame, "t0", object_offset)) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        if (instruction->value.block_id >= function->block_count ||
            fprintf(file, "  la t0, .L%s_core_bb%" PRIu32 "\n", symbol_name, instruction->value.block_id) < 0) return false;
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    /* M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS: the Linux kernel is statically
       linked and executes early global accesses before the MMU is enabled.
       Use the explicit PC-relative pseudo-op so GAS cannot choose GOT form. */
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (instruction->value.global_id >= function->global_count ||
            fprintf(file, "  lla t0, %s\n", function->globals[instruction->value.global_id].name) <
                0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    /* M197_LINUX_STATIC_PCREL_FUNCTION_ADDRESS: early static-kernel code may
       take function addresses while MMU-off (for example set_satp_mode).
       Keep those addresses PC-relative rather than GOT/high-half based. */
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (instruction->value.function_symbol_id >= function->function_symbol_count ||
            fprintf(file,
                    "  lla t0, %s\n",
                    function->function_symbols[instruction->value.function_symbol_id].name) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (minic_type_is_int128_integer(instruction->type) ||
            minic_type_is_long_double(instruction->type)) {
            if (!minic_riscv64_frame_load_value(file, frame, instruction->value.load.address, "t0") ||
                fprintf(file, "  ld t1, 0(t0)\n  ld t2, 8(t0)\n") < 0) {
                return false;
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t1", "t2");
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_scalar_load_for_program(
                file, program, instruction->type, "t1", "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId stored_value;
        MinicType stored_type;

        stored_value = instruction->value.store.stored_value;
        if (stored_value >= function->value_count) {
            return false;
        }
        stored_type = function->values[stored_value].type;
        if (minic_type_is_int128_integer(stored_type) ||
            minic_type_is_long_double(stored_type)) {
            return minic_riscv64_frame_load_value(file, frame, instruction->value.store.address, "t0") &&
                   minic_riscv64_frame_load_int128(file, frame, stored_value, "t1", "t2") &&
                   fprintf(file, "  sd t1, 0(t0)\n  sd t2, 8(t0)\n") >= 0;
        }
        return minic_riscv64_frame_load_value(file, frame, instruction->value.store.address, "t0") &&
               minic_riscv64_frame_load_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_scalar_store_for_program(file, program, stored_type, "t1", "t0");
    }
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const char *opcode;
        size_t destination_offset;
        size_t record_size;

        if (!minic_riscv64_core_record_load_supported(program, function, instruction, &record_size) ||
            !minic_riscv64_core_frame_object_offset(program,
                                function,
                                frame,
                                instruction->value.record_load.destination_object,
                                &destination_offset) ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.record_load.source_address, "t0")) {
            return false;
        }
        if (record_size <= 8U) {
            opcode = record_size == 8U ? "ld" : record_size == 4U ? "lwu" :
                     record_size == 2U ? "lhu" : "lbu";
            if (fprintf(file, "  %s t1, 0(t0)\n", opcode) < 0 ||
                !minic_riscv64_frame_store_chunk(file, frame, "t1", destination_offset, record_size)) {
                return false;
            }
            return true;
        }

        /* M162_CORE_RV64_RECORD_LOAD: materialize arbitrary non-empty records
           into the destination CoreObject without assuming source alignment.
           This mirrors RECORD_COPY's byte-safe O0 fallback. */
        if (!minic_riscv64_frame_emit_address(file, frame, "t1", destination_offset)) {
            return false;
        }
        {
            size_t copied = 0U;
            while (copied < record_size) {
                size_t chunk = record_size - copied;
                size_t offset;
                if (chunk > 2048U) {
                    chunk = 2048U;
                }
                for (offset = 0U; offset < chunk; ++offset) {
                    if (fprintf(file,
                                "  lbu t2, %zu(t0)\n"
                                "  sb t2, %zu(t1)\n",
                                offset,
                                offset) < 0) {
                        return false;
                    }
                }
                copied += chunk;
                if (copied < record_size &&
                    fprintf(file,
                            "  li t3, %zu\n"
                            "  add t0, t0, t3\n"
                            "  add t1, t1, t3\n",
                            chunk) < 0) {
                    return false;
                }
            }
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        size_t alignment;
        size_t copied;
        size_t copy_size;

        if (!minic_riscv64_core_record_copy_supported(program, function, instruction) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    instruction->type,
                                    &copy_size,
                                    &alignment) ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.record_copy.destination_address, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.record_copy.source_address, "t1")) {
            return false;
        }
        (void)alignment;
        copied = 0U;
        while (copied < copy_size) {
            size_t chunk = copy_size - copied;
            size_t offset;
            if (chunk > 2048U) {
                chunk = 2048U;
            }
            for (offset = 0U; offset < chunk; ++offset) {
                if (fprintf(file,
                            "  lbu t2, %zu(t1)\n"
                            "  sb t2, %zu(t0)\n",
                            offset,
                            offset) < 0) {
                    return false;
                }
            }
            copied += chunk;
            if (copied < copy_size &&
                fprintf(file,
                        "  li t3, %zu\n"
                        "  add t0, t0, t3\n"
                        "  add t1, t1, t3\n",
                        chunk) < 0) {
                return false;
            }
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, function, symbol_name, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        return emit_register_output_inline_asm(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return emit_register_output_input_inline_asm(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return emit_memory_readwrite_scalar_input_inline_asm(
            file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return emit_scalar_input_inline_asm(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return emit_structured_inline_asm(file, program, function, frame, instruction, instruction_id);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return minic_riscv64_emit_direct_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return minic_riscv64_emit_indirect_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return emit_field_address(file, frame, instruction);
    }
    return false;
}

static bool emit_block_label(FILE *file, const char *symbol_name, MinicCoreBlockId block_id) {
    return fprintf(file, ".L%s_core_bb%" PRIu32 ":\n", symbol_name, block_id) >= 0;
}

/* Bootstrap B0 reaches functions large enough that a direct JAL relocation can
   overflow its signed 21-bit displacement. Core values are stack-backed at
   terminators, so t6 is a dead scratch register here. Materialize local CFG
   destinations PC-relatively and jump through t6; this keeps block transfers
   valid across the whole practical function-size range instead of relying on
   assembler/linker JAL relaxation. */
static bool emit_far_jump_to_block(FILE *file,
                                   const char *symbol_name,
                                   MinicCoreBlockId block_id) {
    return file != NULL && symbol_name != NULL &&
           fprintf(file,
                   "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                   "  jalr zero, t6, 0\n",
                   symbol_name,
                   block_id) >= 0;
}

static bool emit_far_jump_to_return(FILE *file, const char *symbol_name) {
    return file != NULL && symbol_name != NULL &&
           fprintf(file,
                   "  lla t6, .L%s_core_return\n"
                   "  jalr zero, t6, 0\n",
                   symbol_name) >= 0;
}

static bool emit_terminator(FILE *file,
                            const MinicC0Program *program,
                            const MinicCoreFunction *function,
                            const MinicRiscv64CoreFrame *frame,
                            const char *symbol_name,
                            const MinicCoreTerminator *terminator) {
    if (file == NULL || function == NULL || frame == NULL || symbol_name == NULL ||
        terminator == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (minic_type_is_record(function->return_type)) {
            MinicRiscv64AbiValue return_value;
            size_t object_offset;

            if (program == NULL || terminator->return_object >= function->object_count ||
                !minic_type_equal(function->objects[terminator->return_object].type,
                                  function->return_type) ||
                !minic_riscv64_abi_classify_value(program, function->return_type, &return_value) ||
                !minic_riscv64_core_frame_object_offset(program, function, frame, terminator->return_object, &object_offset) ||
                !minic_riscv64_frame_emit_address(file, frame, "t0", object_offset)) {
                return false;
            }
            if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (return_value.slot_count == 0U || return_value.slot_count > 2U ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, function->return_type, 0U, "a0", "t0") ||
                    (return_value.slot_count == 2U &&
                     !minic_riscv64_emit_integer_aggregate_load_chunk(
                         file, program, function->return_type, 1U, "a1", "t0"))) {
                    return false;
                }
            } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                size_t copied;

                if (!frame->has_hidden_result_pointer || return_value.storage_size <= 16U ||
                    return_value.slot_count != 1U ||
                    !minic_riscv64_frame_emit_load64(
                        file, frame, "t1", frame->hidden_result_pointer_offset)) {
                    return false;
                }
                copied = 0U;
                while (copied < return_value.storage_size) {
                    size_t chunk = return_value.storage_size - copied;
                    size_t offset;

                    if (chunk > 2048U) {
                        chunk = 2048U;
                    }
                    for (offset = 0U; offset < chunk; ++offset) {
                        if (fprintf(file,
                                    "  lbu t2, %zu(t0)\n"
                                    "  sb t2, %zu(t1)\n",
                                    offset,
                                    offset) < 0) {
                            return false;
                        }
                    }
                    copied += chunk;
                    if (copied < return_value.storage_size &&
                        fprintf(file,
                                "  addi t0, t0, 2047\n"
                                "  addi t0, t0, 1\n"
                                "  addi t1, t1, 2047\n"
                                "  addi t1, t1, 1\n") < 0) {
                        return false;
                    }
                }
            } else {
                return false;
            }
        } else if (minic_type_is_long_double(function->return_type)) {
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !minic_riscv64_frame_load_int128(
                    file, frame, terminator->return_value, "a0", "a1")) {
                return false;
            }
        } else if (minic_type_is_float(function->return_type) ||
                   minic_type_is_double(function->return_type)) {
            const char *move_opcode =
                minic_type_is_double(function->return_type) ? "fmv.d.x" : "fmv.w.x";
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !minic_riscv64_frame_load_value(file, frame, terminator->return_value, "a0") ||
                fprintf(file, "  %s fa0, a0\n", move_opcode) < 0) {
                return false;
            }
        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !minic_riscv64_frame_load_value(file, frame, terminator->return_value, "a0")) {
            return false;
        }
        return emit_far_jump_to_return(file, symbol_name);
    /* M91_BUILTIN_UNREACHABLE_TERMINATOR: reaching this block is UB; no
       target instruction is required. The Core terminator still prevents
       normal CFG fallthrough from being modeled as a supported continuation. */
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return true;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return emit_far_jump_to_block(file, symbol_name, terminator->branch_target);
    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_RV64 */
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return minic_riscv64_frame_load_value(file, frame, terminator->indirect_target, "t0") &&
               fprintf(file, "  jalr zero, t0, 0\n") >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        /* Keep the architectural conditional branch local, then use the same
           far-transfer sequence for both CFG successors. The numeric local
           label is intentionally reusable; 1f always resolves to the next
           instance emitted by this terminator. */
        return minic_riscv64_frame_load_value(file, frame, terminator->conditional.condition, "t0") &&
               fprintf(file,
                       "  beqz t0, 1f\n"
                       "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                       "  jalr zero, t6, 0\n"
                       "1:\n"
                       "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                       "  jalr zero, t6, 0\n",
                       symbol_name,
                       terminator->conditional.when_true,
                       symbol_name,
                       terminator->conditional.when_false) >= 0;
    }
    return false;
}

/* M177_CONSTANT_SWITCH_CFG_OWNER: user C labels are valid re-entry
   roots for goto/asm-goto/computed-goto, so seed them conservatively alongside
   the function entry and close over explicit Core terminator edges. Synthetic
   switch blocks with no executable predecessor remain unmarked. */
static bool emit_core_function_with_symbol(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicCoreFunction *function,
                                                    const MinicRiscv64FunctionSymbol *symbol) {
    MinicRiscv64CoreFrame frame;
    const char *symbol_name;
    size_t block_index;
    bool bootstrap_trace;

    if (file == NULL || symbol == NULL || symbol->symbol_name == NULL ||
        symbol->symbol_name[0] == '\0') {
        return false;
    }
    symbol_name = symbol->symbol_name;
    bootstrap_trace = getenv("MINIC_BOOTSTRAP_TRACE") != NULL;
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-capability state=begin "
                      "function=%s blocks=%zu instructions=%zu values=%zu\n",
                      symbol_name,
                      function != NULL ? function->block_count : 0U,
                      function != NULL ? function->instruction_count : 0U,
                      function != NULL ? function->value_count : 0U);
        (void)fflush(stderr);
    }
    if (!core_function_can_emit(program, function)) {
        return false;
    }
    memset(&frame, 0, sizeof(frame));
    minic_riscv64_object_liveness_begin_function();
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-capability state=end "
                      "function=%s\n",
                      symbol_name);
        (void)fflush(stderr);
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-frame state=begin "
                      "function=%s\n",
                      symbol_name);
        (void)fflush(stderr);
    }
    /* Opt-in P1 test changes only the selected function's frame. */
    {
        const char *focus = getenv("MINIC_P1_CROSSBLOCK_FUNCTION");
        minic_riscv64_object_liveness_set_crossblock_enabled(
            focus == NULL || focus[0] == 0 ||
            strcmp(focus, symbol_name) == 0);
    }
    if (!minic_riscv64_asm_function_plan_build(function, &frame.asm_plan) ||
        !minic_riscv64_core_frame_initialize(program, function, &frame)) {
        return core_frame_fail(&frame);
    }
    if (!minic_riscv64_core_frame_mark_reachable_blocks(function, &frame)) {
        return core_frame_fail(&frame);
    }
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-frame state=end "
                      "function=%s frame_size=%zu value_slots=%zu values=%zu\n",
                      symbol_name,
                      frame.frame_size,
                      frame.value_slot_count,
                      frame.value_count);
        (void)fflush(stderr);
    }
    if (!minic_riscv64_emit_function_symbol_begin(file, symbol)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_dynamic_stack_alignment) {
        if (fprintf(file, "  mv t0, sp\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.frame_size) ||
            fprintf(file,
                    "  li t1, -%zu\n"
                    "  and sp, sp, t1\n",
                    frame.stack_alignment) < 0 ||
            !minic_riscv64_emit_sp_store64(file, "t0", frame.entry_sp_offset)) {
            return core_frame_fail(&frame);
        }
    } else if (!minic_riscv64_emit_stack_allocate(file, frame.frame_size)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_dynamic_stack_alloc) {
        if (!minic_riscv64_emit_sp_store64(
                file, "s0", frame.saved_frame_base_offset) ||
            fprintf(file, "  mv s0, sp\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.dynamic_call_area_size)) {
            return core_frame_fail(&frame);
        }
    }
    if (frame.saves_return_address &&
        !minic_riscv64_frame_emit_store64(file, &frame, "ra", frame.return_address_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_hidden_result_pointer &&
        !minic_riscv64_frame_emit_store64(file, &frame, "a0", frame.hidden_result_pointer_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        size_t saved_slot = 0U;
        for (saved_index = 0U; saved_index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset;
            if ((frame.asm_plan.callee_saved_mask &
                 (uint16_t)(1U << saved_index)) == 0U) {
                continue;
            }
            saved_offset = frame.structured_asm_callee_saved_offset + saved_slot * 8U;
            ++saved_slot;
            if (!minic_riscv64_frame_emit_store64(
                    file, &frame, minic_riscv64_asm_callee_saved_registers[saved_index], saved_offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (frame.has_variadic_argument_address) {
        size_t register_index;

        for (register_index = frame.integer_parameter_count; register_index < 8U;
             ++register_index) {
            size_t offset = frame.varargs_offset +
                            (register_index - frame.integer_parameter_count) * 8U;
            if (!minic_riscv64_frame_emit_store64(
                    file, &frame, minic_riscv64_abi_integer_argument_registers[register_index], offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (!emit_far_jump_to_block(file, symbol_name, function->entry_block)) {
        return core_frame_fail(&frame);
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block;
        size_t instruction_index;

        if (bootstrap_trace &&
            (block_index == 0U || block_index % 128U == 0U ||
             block_index + 1U == function->block_count)) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-codegen-block "
                          "function=%s block=%zu total=%zu\n",
                          symbol_name,
                          block_index,
                          function->block_count);
            (void)fflush(stderr);
        }
        block = &function->blocks[block_index];
        if (frame.reachable_blocks != NULL && !frame.reachable_blocks[block_index]) {
            continue;
        }
        if (!emit_block_label(file, symbol_name, (MinicCoreBlockId)block_index)) {
            return core_frame_fail(&frame);
        }
        if (!function->suppress_source_label_aliases &&
            block->source_label_id != SIZE_MAX &&
            fprintf(file, ".Luser_%zu:\n", block->source_label_id) < 0) {
            return core_frame_fail(&frame);
        }
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            MinicCoreInstructionId instruction_id;

            instruction_id = block->instructions[instruction_index];
            if (instruction_id >= function->instruction_count ||
                !emit_instruction(file, program, function, &frame, symbol_name,
                                  &function->instructions[instruction_id], (size_t)instruction_id)) {
                return core_frame_fail(&frame);
            }
        }
        if (!block->has_terminator ||
            !emit_terminator(file, program, function, &frame, symbol_name, &block->terminator)) {
            return core_frame_fail(&frame);
        }
    }
    if (fprintf(file, ".L%s_core_return:\n", symbol_name) < 0) {
        return core_frame_fail(&frame);
    }
    if (frame.saves_return_address &&
        !minic_riscv64_frame_emit_load64(file, &frame, "ra", frame.return_address_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        size_t saved_slot = 0U;
        for (saved_index = 0U; saved_index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset;
            if ((frame.asm_plan.callee_saved_mask &
                 (uint16_t)(1U << saved_index)) == 0U) {
                continue;
            }
            saved_offset = frame.structured_asm_callee_saved_offset + saved_slot * 8U;
            ++saved_slot;
            if (!minic_riscv64_frame_emit_load64(
                    file, &frame, minic_riscv64_asm_callee_saved_registers[saved_index], saved_offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (frame.has_dynamic_stack_alloc) {
        if (!minic_riscv64_frame_emit_load64(file, &frame, "t4", frame.saved_frame_base_offset)) {
            return core_frame_fail(&frame);
        }
        if (frame.has_dynamic_stack_alignment) {
            if (!minic_riscv64_frame_emit_load64(file, &frame, "t0", frame.entry_sp_offset)) {
                return core_frame_fail(&frame);
            }
        } else if (frame.frame_size <= 2047U) {
            if (fprintf(file, "  addi t0, s0, %zu\n", frame.frame_size) < 0) {
                return core_frame_fail(&frame);
            }
        } else if (fprintf(file,
                           "  li t0, %zu\n"
                           "  add t0, s0, t0\n",
                           frame.frame_size) < 0) {
            return core_frame_fail(&frame);
        }
        if (fprintf(file,
                    "  mv sp, t0\n"
                    "  mv s0, t4\n") < 0) {
            return core_frame_fail(&frame);
        }
    } else if (frame.has_dynamic_stack_alignment) {
        if (!minic_riscv64_emit_sp_load64(file, "t0", frame.entry_sp_offset) ||
            fprintf(file, "  mv sp, t0\n") < 0) {
            return core_frame_fail(&frame);
        }
    } else if (!minic_riscv64_emit_stack_release(file, frame.frame_size)) {
        return core_frame_fail(&frame);
    }
    if (fprintf(file, "  ret\n") < 0 ||
        !minic_riscv64_emit_function_symbol_end(file, symbol)) {
        return core_frame_fail(&frame);
    }
    minic_riscv64_core_frame_destroy(&frame);
    return true;
}

bool minic_riscv64_emit_core_function_with_symbol(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64FunctionSymbol *symbol) {
    return emit_core_function_with_symbol(file, NULL, function, symbol);
}

bool minic_riscv64_emit_core_function_for_program_with_symbol(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64FunctionSymbol *symbol) {
    return program != NULL &&
           emit_core_function_with_symbol(file, program, function, symbol);
}
