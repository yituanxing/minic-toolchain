#include "target/riscv64/core_codegen.h"

#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"
#include "target/data_layout.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const minic_core_rv64_argument_registers[8] = {
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
};

static const char *const minic_core_rv64_floating_argument_registers[8] = {
    "fa0",
    "fa1",
    "fa2",
    "fa3",
    "fa4",
    "fa5",
    "fa6",
    "fa7",
};

/* M172_STRUCTURED_ASM_CALLEE_SAVED: basic-v0 conservatively preserves the
   complete RV64 callee-saved bank for functions containing structured asm.
   This lets asm operands fall back to s-registers when every caller-saved
   candidate is clobbered without making Core itself a register allocator. */
static const char *const core_asm_callee_saved_registers[] = {
    "s0", "s1", "s2", "s3", "s4", "s5",
    "s6", "s7", "s8", "s9", "s10", "s11",
};
#define CORE_ASM_CALLEE_SAVED_COUNT     (sizeof(core_asm_callee_saved_registers) / sizeof(core_asm_callee_saved_registers[0]))

typedef struct MinicRiscv64CoreFrame {
    size_t frame_size;
    size_t object_count;
    size_t value_count;
    size_t value_base_offset;
    size_t outgoing_argument_size;
    size_t return_address_offset;
    /* M167D_INDIRECT_RECORD_RETURN: psABI hidden result pointer is incoming
       state and must survive arbitrary calls before a Core RETURN. */
    size_t hidden_result_pointer_offset;
    size_t entry_sp_offset;
    size_t stack_alignment;
    size_t structured_asm_callee_saved_offset;
    size_t varargs_offset;
    size_t varargs_size;
    size_t integer_parameter_count;
    size_t variadic_fixed_stack_slots;
    bool saves_return_address;
    bool has_hidden_result_pointer;
    bool has_dynamic_stack_alignment;
    bool preserves_structured_asm_callee_saved;
    bool has_variadic_argument_address;
} MinicRiscv64CoreFrame;

static bool core_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_float(type) || minic_type_is_double(type);
}

static const MinicCoreFixedRegisterBinding *core_fixed_register_binding(
    const MinicCoreFunction *function, size_t binding_id) {
    return function != NULL && binding_id < function->fixed_register_binding_count
               ? &function->fixed_register_bindings[binding_id]
               : NULL;
}

static bool core_effective_integer_type(const MinicCoreFunction *function,
                                        MinicType type,
                                        MinicType *effective_type) {
    return minic_core_function_effective_integer_type(function, type, effective_type);
}

/* M74_GLOBAL_RECORD_ADDRESS / M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER:
   RV64 global.addr lowers to `la symbol`; the pointee's storage shape is
   irrelevant until a later memory operation.  A declaration-only void symbol
   therefore needs no new load/store ABI support. */
static bool core_global_addressable_type(MinicType type) {
    return core_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type) || minic_type_is_void(type);
}

static bool align_up(size_t value, size_t alignment, size_t *result) {
    size_t remainder;

    if (result == NULL || alignment == 0U) {
        return false;
    }
    remainder = value % alignment;
    if (remainder == 0U) {
        *result = value;
        return true;
    }
    if (value > SIZE_MAX - (alignment - remainder)) {
        return false;
    }
    *result = value + (alignment - remainder);
    return true;
}

/* M79_CALL_FRAME_RETURN_ADDRESS: a return-address query needs the entry
   value of ra even in a function that has no ordinary Core CALL yet. Save it
   in the prologue whenever either a call or this semantic instruction exists. */
static bool core_function_needs_saved_return_address(const MinicCoreFunction *function) {
    size_t instruction_index;

    if (function == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        MinicCoreInstructionKind kind = function->instructions[instruction_index].kind;
        if (kind == MINIC_CORE_INSTRUCTION_CALL ||
            kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
            return true;
        }
        if (kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS &&
            function->instructions[instruction_index].value.call_frame_address.kind ==
                MINIC_CORE_CALL_FRAME_ADDRESS_RETURN) {
            return true;
        }
    }
    return false;
}

static bool core_function_uses_structured_inline_asm(
    const MinicCoreFunction *function) {
    size_t instruction_index;

    if (function == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        if (function->instructions[instruction_index].kind ==
            MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
            return true;
        }
    }
    return false;
}

static bool core_function_uses_variadic_argument_address(
    const MinicCoreFunction *function) {
    size_t instruction_index;

    if (function == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        if (function->instructions[instruction_index].kind ==
            MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) {
            return true;
        }
    }
    return false;
}

static bool core_variadic_fixed_prefix(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       size_t *integer_parameter_count,
                                       size_t *fixed_stack_slots) {
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t parameter_index;

    if (program == NULL || function == NULL || integer_parameter_count == NULL ||
        fixed_stack_slots == NULL ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, function->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        MinicRiscv64AbiArgumentLocation location;

        if (!minic_riscv64_abi_place_argument(
                program, function->parameter_types[parameter_index], true, &cursor, &location)) {
            return false;
        }
    }
    /* RV64 varargs are contiguous after the named prefix. If integer argument
       registers remain, preserve them in the callee frame as before. Once the
       named prefix has exhausted a0-a7, the first unnamed argument starts
       directly after the named incoming stack slots. Keep mixed register/stack
       prefixes fail-closed unless all integer argument registers are consumed. */
    if (cursor.integer_register_count > 8U ||
        (cursor.stack_slot_count != 0U && cursor.integer_register_count != 8U)) {
        return false;
    }
    *integer_parameter_count = cursor.integer_register_count;
    *fixed_stack_slots = cursor.stack_slot_count;
    return true;
}

/* M168_RV64_INDIRECT_CALL_STACK: reserve one fixed outgoing stack area
   shared by direct and indirect calls.  The ABI cursor remains the single owner
   of register/stack placement; Core keeps only VALUE/OBJECT arguments. */
static bool core_call_outgoing_stack_size(const MinicC0Program *program,
                                          const MinicCoreFunction *function,
                                          size_t *result) {
    size_t instruction_index;
    size_t maximum_stack_slots;

    if (program == NULL || function == NULL || result == NULL) {
        return false;
    }
    maximum_stack_slots = 0U;
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        const MinicCoreInstruction *instruction = &function->instructions[instruction_index];
        const MinicType *parameter_types;
        size_t parameter_count;
        bool is_variadic;
        size_t argument_begin;
        size_t argument_count;
        MinicType return_type;
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;
        size_t argument_index;

        if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
            const MinicCoreCallee *callee;
            if (instruction->value.call.callee_id >= function->callee_count ||
                instruction->value.call.argument_begin > function->call_argument_count ||
                instruction->value.call.argument_count >
                    function->call_argument_count - instruction->value.call.argument_begin) {
                return false;
            }
            callee = &function->callees[instruction->value.call.callee_id];
            parameter_types = callee->parameter_types;
            parameter_count = callee->parameter_count;
            is_variadic = callee->is_variadic;
            argument_begin = instruction->value.call.argument_begin;
            argument_count = instruction->value.call.argument_count;
            return_type = callee->return_type;
        } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
            const MinicCoreCallSignature *signature;
            if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
                instruction->value.indirect_call.argument_begin > function->call_argument_count ||
                instruction->value.indirect_call.argument_count >
                    function->call_argument_count - instruction->value.indirect_call.argument_begin) {
                return false;
            }
            signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
            parameter_types = signature->parameter_types;
            parameter_count = signature->parameter_count;
            is_variadic = signature->is_variadic;
            argument_begin = instruction->value.indirect_call.argument_begin;
            argument_count = instruction->value.indirect_call.argument_count;
            return_type = signature->return_type;
        } else {
            continue;
        }

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, return_type, &cursor, &return_value)) {
            return false;
        }
        (void)return_value;
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicCoreCallArgument *argument =
                &function->call_arguments[argument_begin + argument_index];
            MinicRiscv64AbiArgumentLocation location;
            MinicType argument_type;
            bool is_fixed_parameter = argument_index < parameter_count;

            if (is_fixed_parameter) {
                argument_type = parameter_types[argument_index];
            } else {
                if (!is_variadic) {
                    return false;
                }
                if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    if (argument->value.value_id >= function->value_count) {
                        return false;
                    }
                    argument_type = function->values[argument->value.value_id].type;
                } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    if (argument->value.object_id >= function->object_count ||
                        !minic_type_is_record(
                            function->objects[argument->value.object_id].type)) {
                        return false;
                    }
                    argument_type = function->objects[argument->value.object_id].type;
                } else {
                    return false;
                }
            }
            if (!minic_riscv64_abi_place_argument(
                    program, argument_type, is_fixed_parameter, &cursor, &location)) {
                return false;
            }
        }
        if (cursor.stack_slot_count > maximum_stack_slots) {
            maximum_stack_slots = cursor.stack_slot_count;
        }
    }
    if (maximum_stack_slots > SIZE_MAX / 8U) {
        return false;
    }
    *result = maximum_stack_slots * 8U;
    return true;
}

static bool core_frame_initialize(const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  MinicRiscv64CoreFrame *frame) {
    size_t object_index;
    size_t storage_size;
    size_t required_size;
    size_t outgoing_argument_size;
    size_t maximum_object_alignment;

    if (function == NULL || frame == NULL ||
        !core_call_outgoing_stack_size(program, function, &outgoing_argument_size)) {
        return false;
    }
    frame->outgoing_argument_size = outgoing_argument_size;
    storage_size = outgoing_argument_size;
    maximum_object_alignment = 16U;
    for (object_index = 0U; object_index < function->object_count; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count) {
            return false;
        }
        if (function->objects[object_index].explicit_alignment != 0U) {
            size_t explicit_alignment = function->objects[object_index].explicit_alignment;

            if ((explicit_alignment & (explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if (explicit_alignment > object_alignment) {
                object_alignment = explicit_alignment;
            }
        }
        if (object_alignment > maximum_object_alignment) {
            maximum_object_alignment = object_alignment;
        }
        object_size *= function->objects[object_index].element_count;
        if (!align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;
    }
    if (!align_up(storage_size, 8U, &frame->value_base_offset) ||
        function->value_count > (SIZE_MAX - frame->value_base_offset) / 16U) {
        return false;
    }
    /* M161_CORE_RV64_INT128_PAIR: one O0 spill slot can hold the largest
       current Core scalar. i128 uses low64 at +0/high64 at +8. */
    storage_size = frame->value_base_offset + function->value_count * 16U;
    frame->saves_return_address = core_function_needs_saved_return_address(function);
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }

    frame->has_hidden_result_pointer = false;
    frame->hidden_result_pointer_offset = 0U;
    if (program != NULL) {
        MinicRiscv64AbiCursor return_cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &return_cursor, &return_value)) {
            return false;
        }
        (void)return_cursor;
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U ||
                !align_up(storage_size, 8U, &frame->hidden_result_pointer_offset) ||
                frame->hidden_result_pointer_offset > SIZE_MAX - 8U) {
                return false;
            }
            storage_size = frame->hidden_result_pointer_offset + 8U;
            frame->has_hidden_result_pointer = true;
        }
    }

    frame->preserves_structured_asm_callee_saved =
        core_function_uses_structured_inline_asm(function);
    frame->structured_asm_callee_saved_offset = 0U;
    if (frame->preserves_structured_asm_callee_saved) {
        size_t saved_bytes = CORE_ASM_CALLEE_SAVED_COUNT * 8U;
        if (!align_up(storage_size, 8U, &frame->structured_asm_callee_saved_offset) ||
            frame->structured_asm_callee_saved_offset > SIZE_MAX - saved_bytes) {
            return false;
        }
        storage_size = frame->structured_asm_callee_saved_offset + saved_bytes;
    }

    frame->stack_alignment = maximum_object_alignment;
    frame->has_dynamic_stack_alignment = maximum_object_alignment > 16U;
    frame->entry_sp_offset = 0U;
    if (frame->has_dynamic_stack_alignment) {
        if (!align_up(storage_size, 8U, &frame->entry_sp_offset) ||
            frame->entry_sp_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->entry_sp_offset + 8U;
    }

    frame->has_variadic_argument_address =
        core_function_uses_variadic_argument_address(function);
    frame->integer_parameter_count = 0U;
    frame->variadic_fixed_stack_slots = 0U;
    frame->varargs_size = 0U;
    if (frame->has_variadic_argument_address) {
        if (!core_variadic_fixed_prefix(program,
                                        function,
                                        &frame->integer_parameter_count,
                                        &frame->variadic_fixed_stack_slots)) {
            return false;
        }
        frame->varargs_size = (8U - frame->integer_parameter_count) * 8U;
    }
    if (storage_size > SIZE_MAX - frame->varargs_size) {
        return false;
    }
    required_size = storage_size + frame->varargs_size;
    if (!align_up(required_size, 16U, &frame->frame_size) ||
        frame->frame_size < frame->varargs_size) {
        return false;
    }
    frame->varargs_offset = frame->frame_size - frame->varargs_size;
    if (frame->varargs_offset < storage_size) {
        return false;
    }
    frame->object_count = function->object_count;
    frame->value_count = function->value_count;
    return true;
}

static bool core_object_offset(const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               const MinicRiscv64CoreFrame *frame,
                               MinicCoreObjectId object_id,
                               size_t *offset) {
    size_t current_offset;
    size_t object_index;

    if (function == NULL || frame == NULL || offset == NULL ||
        object_id >= function->object_count) {
        return false;
    }
    current_offset = frame->outgoing_argument_size;
    for (object_index = 0U; object_index <= (size_t)object_id; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count) {
            return false;
        }
        if (function->objects[object_index].explicit_alignment != 0U) {
            size_t explicit_alignment = function->objects[object_index].explicit_alignment;

            if ((explicit_alignment & (explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if (explicit_alignment > object_alignment) {
                object_alignment = explicit_alignment;
            }
        }
        if (!align_up(current_offset, object_alignment, &current_offset)) {
            return false;
        }
        if (object_index == (size_t)object_id) {
            *offset = current_offset;
            return true;
        }
        object_size *= function->objects[object_index].element_count;
        if (current_offset > SIZE_MAX - object_size) {
            return false;
        }
        current_offset += object_size;
    }
    return false;
}

static bool
core_value_offset(const MinicRiscv64CoreFrame *frame, MinicCoreValueId value_id, size_t *offset) {
    if (frame == NULL || offset == NULL || value_id >= frame->value_count ||
        (size_t)value_id > (SIZE_MAX - frame->value_base_offset) / 16U) {
        return false;
    }
    *offset = frame->value_base_offset + (size_t)value_id * 16U;
    return true;
}

static bool emit_sp_address(FILE *file, const char *destination_register, size_t offset) {
    if (file == NULL || destination_register == NULL) {
        return false;
    }
    if (offset <= 2047U) {
        return fprintf(file, "  addi %s, sp, %zu\n", destination_register, offset) >= 0;
    }
    return fprintf(file,
                   "  li t3, %zu\n"
                   "  add %s, sp, t3\n",
                   offset,
                   destination_register) >= 0;
}

static bool load_core_value(FILE *file,
                            const MinicRiscv64CoreFrame *frame,
                            MinicCoreValueId value_id,
                            const char *register_name) {
    size_t offset;

    return core_value_offset(frame, value_id, &offset) &&
           minic_riscv64_emit_sp_load64(file, register_name, offset);
}

static bool store_core_value(FILE *file,
                             const MinicRiscv64CoreFrame *frame,
                             MinicCoreValueId value_id,
                             const char *register_name) {
    size_t offset;

    return core_value_offset(frame, value_id, &offset) &&
           minic_riscv64_emit_sp_store64(file, register_name, offset);
}

/* M161_CORE_RV64_INT128_PAIR: Core remains target-neutral; RV64 lowers wide
   integer values to a low/high XLEN pair only inside the target backend. */
static bool load_core_int128_value(FILE *file,
                                   const MinicRiscv64CoreFrame *frame,
                                   MinicCoreValueId value_id,
                                   const char *low_register,
                                   const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           core_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           minic_riscv64_emit_sp_load64(file, low_register, offset) &&
           minic_riscv64_emit_sp_load64(file, high_register, offset + 8U);
}

static bool store_core_int128_value(FILE *file,
                                    const MinicRiscv64CoreFrame *frame,
                                    MinicCoreValueId value_id,
                                    const char *low_register,
                                    const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           core_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           minic_riscv64_emit_sp_store64(file, low_register, offset) &&
           minic_riscv64_emit_sp_store64(file, high_register, offset + 8U);
}

static bool core_integer_type_is_signed(const MinicCoreFunction *function,
                                        MinicType type,
                                        bool *is_signed) {
    MinicType effective_type;

    if (is_signed == NULL ||
        !minic_core_function_effective_integer_type(function, type, &effective_type)) {
        return false;
    }
    *is_signed = minic_type_is_signed_integer(effective_type);
    return true;
}

static bool core_unsigned_integer_width(const MinicC0Program *program,
                                        const MinicCoreFunction *function,
                                        MinicCoreValueId value_id,
                                        unsigned int *width) {
    size_t size;
    size_t alignment;
    MinicType type;

    if (program == NULL || function == NULL || width == NULL ||
        value_id >= function->value_count) {
        return false;
    }
    type = function->values[value_id].type;
    if (!minic_type_is_unsigned_integer(type) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, type, &size, &alignment) ||
        size == 0U || size > 8U) {
        return false;
    }
    (void)alignment;
    *width = (unsigned int)(size * 8U);
    return true;
}

static bool core_field_address_supported(const MinicCoreInstruction *instruction,
                                         size_t *field_offset) {
    if (instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_FIELD_ADDRESS) {
        return false;
    }
    if (field_offset != NULL) {
        *field_offset = instruction->value.field_address.byte_offset;
    }
    return true;
}

static bool core_record_load_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       size_t *record_size) {
    MinicCoreObjectId destination_object;
    MinicCoreValueId source_address;
    MinicType record_type;
    MinicType source_pointee;
    MinicType source_type;
    size_t alignment;
    size_t size;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_record(instruction->type) ||
        !minic_type_unqualified(instruction->type, &record_type) ||
        !minic_type_equal(record_type, instruction->type)) {
        return false;
    }
    destination_object = instruction->value.record_load.destination_object;
    source_address = instruction->value.record_load.source_address;
    if (destination_object >= function->object_count || source_address >= function->value_count ||
        !minic_type_equal(function->objects[destination_object].type, instruction->type) ||
        !minic_type_pointee(function->values[source_address].type, &source_pointee) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(source_type, instruction->type) ||
        instruction->value.record_load.is_volatile != minic_type_is_volatile(source_pointee) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, instruction->type, &size, &alignment) ||
        size == 0U) {
        return false;
    }
    (void)alignment;
    if (record_size != NULL) {
        *record_size = size;
    }
    return true;
}

static bool core_record_copy_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    MinicType destination_pointee;
    MinicType destination_type;
    MinicType source_pointee;
    MinicType source_type;
    size_t alignment;
    size_t size;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||
        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_record(instruction->type) ||
        instruction->value.record_copy.destination_address >= function->value_count ||
        instruction->value.record_copy.source_address >= function->value_count ||
        !minic_type_pointee(
            function->values[instruction->value.record_copy.destination_address].type,
            &destination_pointee) ||
        !minic_type_pointee(function->values[instruction->value.record_copy.source_address].type,
                            &source_pointee) ||
        !minic_type_unqualified(destination_pointee, &destination_type) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(destination_type, instruction->type) ||
        !minic_type_equal(source_type, instruction->type) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, instruction->type, &size, &alignment)) {
        return false;
    }
    /* M167_ZERO_RECORD_COPY: GNU empty records are addressable semantic
       objects. RECORD_COPY has already evaluated both address operands; with
       zero storage bytes the target action is an intentional no-op. */
    return alignment != 0U;
}

static bool core_call_frame_address_supported(
    const MinicCoreInstruction *instruction) {
    MinicType pointee;

    return instruction != NULL &&
           instruction->kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS &&
           (instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
            instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
           instruction->value.call_frame_address.level == 0U &&
           minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);
}

static bool core_scalar_bitcast_supported(const MinicC0Program *program,
                                          const MinicCoreFunction *function,
                                          const MinicCoreInstruction *instruction) {
    const MinicCoreValue *source;
    size_t source_size;
    size_t source_alignment;
    size_t target_size;
    size_t target_alignment;
    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||
        instruction->value.operand >= function->value_count) {
        return false;
    }
    source = &function->values[instruction->value.operand];
    if (!minic_core_scalar_bitcast_types_valid(instruction->type, source->type) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, source->type, &source_size, &source_alignment) ||
        !minic_data_layout_type(minic_default_data_layout(),
                                program,
                                instruction->type,
                                &target_size,
                                &target_alignment)) {
        return false;
    }
    (void)source_alignment;
    (void)target_alignment;
    return source_size != 0U && source_size <= 8U && target_size != 0U && target_size <= 8U;
}

static bool core_integer_type_range_fits(const MinicC0Program *program,
                                         const MinicCoreFunction *function,
                                         MinicType source_type,
                                         MinicType result_type) {
    MinicType effective_source;
    MinicType effective_result;
    size_t source_alignment;
    size_t source_size;
    size_t result_alignment;
    size_t result_size;
    bool source_signed;
    bool result_signed;

    if (program == NULL || function == NULL ||
        !minic_type_is_integer(source_type) || !minic_type_is_integer(result_type) ||
        !minic_core_function_effective_integer_type(function, source_type, &effective_source) ||
        !minic_core_function_effective_integer_type(function, result_type, &effective_result) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, source_type, &source_size, &source_alignment) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, result_type, &result_size, &result_alignment) ||
        source_size == 0U || source_size > 8U || result_size == 0U || result_size > 8U) {
        return false;
    }
    (void)source_alignment;
    (void)result_alignment;
    source_signed = minic_type_is_signed_integer(effective_source);
    result_signed = minic_type_is_signed_integer(effective_result);
    if ((!source_signed && !minic_type_is_unsigned_integer(effective_source)) ||
        (!result_signed && !minic_type_is_unsigned_integer(effective_result))) {
        return false;
    }
    if (source_signed == result_signed) {
        return source_size <= result_size;
    }
    /* Every unsigned N-bit value fits a signed result only when the result has
       strictly more value bits. A signed range never wholly fits unsigned. */
    return !source_signed && result_signed && source_size < result_size;
}

static bool core_integer_overflow_xlen_scratch_exact(
    const MinicC0Program *program,
    MinicType left_type,
    MinicType right_type,
    size_t result_size) {
    size_t left_alignment;
    size_t left_size;
    size_t right_alignment;
    size_t right_size;

    if (program == NULL || result_size == 0U || result_size >= 8U ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, left_type, &left_size, &left_alignment) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, right_type, &right_size, &right_alignment)) {
        return false;
    }
    (void)left_alignment;
    (void)right_alignment;
    return left_size != 0U && right_size != 0U &&
           left_size <= result_size && right_size <= result_size;
}

static bool core_integer_overflow_supported(const MinicC0Program *program,
                                            const MinicCoreFunction *function,
                                            const MinicCoreInstruction *instruction,
                                            MinicType *result_type,
                                            size_t *result_size,
                                            bool *is_unsigned) {
    MinicType effective_left_type;
    MinicType effective_result_type;
    MinicType effective_right_type;
    MinicType left_type;
    MinicType pointee;
    MinicType right_type;
    size_t alignment;
    size_t left_alignment;
    size_t left_size;
    size_t right_alignment;
    size_t right_size;
    bool result_is_unsigned;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        !minic_type_equal(instruction->type, minic_type_bool()) ||
        (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
         instruction->value.integer_overflow.operator_kind !=
             MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
         instruction->value.integer_overflow.operator_kind !=
             MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
        instruction->value.integer_overflow.left >= function->value_count ||
        instruction->value.integer_overflow.right >= function->value_count ||
        instruction->value.integer_overflow.result_address >= function->value_count ||
        !minic_type_pointee(
            function->values[instruction->value.integer_overflow.result_address].type, &pointee) ||
        !minic_type_is_integer(pointee) || minic_type_is_bool_integer(pointee) ||
        minic_type_is_const(pointee) || minic_type_is_volatile(pointee) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, pointee, result_size, &alignment) ||
        *result_size == 0U || *result_size > 8U ||
        !minic_core_function_effective_integer_type(function, pointee, &effective_result_type)) {
        return false;
    }
    (void)alignment;
    left_type = function->values[instruction->value.integer_overflow.left].type;
    right_type = function->values[instruction->value.integer_overflow.right].type;
    if (!minic_type_is_integer(left_type) || !minic_type_is_integer(right_type) ||
        !minic_core_function_effective_integer_type(function, left_type, &effective_left_type) ||
        !minic_core_function_effective_integer_type(function, right_type, &effective_right_type) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, left_type, &left_size, &left_alignment) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, right_type, &right_size, &right_alignment) ||
        left_size == 0U || left_size > 8U || right_size == 0U || right_size > 8U) {
        return false;
    }
    (void)left_alignment;
    (void)right_alignment;
    result_is_unsigned = minic_type_is_unsigned_integer(effective_result_type);

    if ((!minic_type_equal(left_type, pointee) || !minic_type_equal(right_type, pointee)) &&
        !(core_integer_type_range_fits(program, function, left_type, pointee) &&
          core_integer_type_range_fits(program, function, right_type, pointee)) &&
        !core_integer_overflow_xlen_scratch_exact(
            program, left_type, right_type, *result_size)) {
        bool left_is_result;
        bool right_is_result;
        const MinicType *other_effective_type;

        /* Preserve the established narrow signed-result extension for the
           one mixed case whose full unsigned operand range does not fit. */
        left_is_result = minic_type_equal(left_type, pointee);
        right_is_result = minic_type_equal(right_type, pointee);
        if ((instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_ADD &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
            result_is_unsigned || *result_size >= 8U || left_is_result == right_is_result ||
            !minic_type_is_signed_integer(effective_result_type)) {
            return false;
        }
        other_effective_type =
            left_is_result ? &effective_right_type : &effective_left_type;
        if (!minic_type_is_unsigned_integer(*other_effective_type)) {
            return false;
        }
        if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY &&
            left_size > 8U - right_size) {
            return false;
        }
    }
    if (result_type != NULL) {
        *result_type = pointee;
    }
    if (is_unsigned != NULL) {
        *is_unsigned = result_is_unsigned;
    }
    return true;
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

static bool core_inline_asm_clobbers_register(const MinicCoreInlineAsm *inline_asm,
                                               const char *register_name) {
    size_t index;
    size_t name_length;

    if (inline_asm == NULL || register_name == NULL) {
        return true;
    }
    name_length = strlen(register_name);
    for (index = 0U; index < inline_asm->register_clobber_count; ++index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[index];
        if (clobber->name != NULL && clobber->name_length == name_length &&
            memcmp(clobber->name, register_name, name_length) == 0) {
            return true;
        }
    }
    return false;
}


typedef struct MinicCoreRiscv64AsmRegisterCandidate {
    const char *name;
} MinicCoreRiscv64AsmRegisterCandidate;

static const MinicCoreRiscv64AsmRegisterCandidate core_asm_caller_saved_registers[] = {
    {"t0"}, {"t1"}, {"t2"}, {"t3"}, {"t4"}, {"t5"}, {"t6"},
    {"a0"}, {"a1"}, {"a2"}, {"a3"}, {"a4"}, {"a5"}, {"a6"}, {"a7"},
};

static bool core_asm_register_name_equal(const char *left, const char *right) {
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static bool core_asm_register_is_caller_saved(const char *name) {
    size_t index;
    for (index = 0U;
         index < sizeof(core_asm_caller_saved_registers) /
                     sizeof(core_asm_caller_saved_registers[0]);
         ++index) {
        if (core_asm_register_name_equal(name, core_asm_caller_saved_registers[index].name)) {
            return true;
        }
    }
    return false;
}


static bool core_asm_register_is_callee_saved(const char *name) {
    size_t index;
    for (index = 0U; index < CORE_ASM_CALLEE_SAVED_COUNT; ++index) {
        if (core_asm_register_name_equal(name, core_asm_callee_saved_registers[index])) {
            return true;
        }
    }
    return false;
}

static bool core_asm_register_in_use(const char *const *operand_registers,
                                     size_t operand_count,
                                     const char *name) {
    size_t index;
    if (operand_registers == NULL || name == NULL) {
        return true;
    }
    for (index = 0U; index < operand_count; ++index) {
        if (core_asm_register_name_equal(operand_registers[index], name)) {
            return true;
        }
    }
    return false;
}

static const char *core_asm_choose_register(const MinicCoreInlineAsm *inline_asm,
                                            const char *const *operand_registers,
                                            size_t operand_count,
                                            const char *const *preferences,
                                            size_t preference_count) {
    size_t index;
    for (index = 0U; index < preference_count; ++index) {
        const char *candidate = preferences[index];
        if (!core_inline_asm_clobbers_register(inline_asm, candidate) &&
            !core_asm_register_in_use(operand_registers, operand_count, candidate)) {
            return candidate;
        }
    }
    return NULL;
}

/* M166_RV64_FIXED_ASM_PHASE_ALIAS: two distinct fixed-register operands may
   intentionally share one architectural register when one is a write-only
   output and the other is a scalar input. Their lifetimes are disjoint:
   input before asm, output after asm. Early-clobber outputs cannot alias. */
static bool core_structured_fixed_phase_alias_safe(
    const MinicCoreInstruction *instruction,
    size_t current_binding_index,
    const char *const *operand_registers,
    const char *register_name) {
    const MinicCoreStructuredInlineAsmOperand *current;
    size_t alias_count;
    size_t prior_index;

    if (instruction == NULL || operand_registers == NULL || register_name == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        current_binding_index >= instruction->value.structured_inline_asm.operand_count) {
        return false;
    }
    current = &instruction->value.structured_inline_asm.operands[current_binding_index];
    if (!current->has_fixed_register_binding) {
        return false;
    }
    alias_count = 0U;
    for (prior_index = 0U; prior_index < current_binding_index; ++prior_index) {
        const MinicCoreStructuredInlineAsmOperand *prior =
            &instruction->value.structured_inline_asm.operands[prior_index];
        bool current_is_input;
        bool current_is_output;
        bool prior_is_input;
        bool prior_is_output;
        const MinicCoreStructuredInlineAsmOperand *output;

        if (!prior->has_fixed_register_binding || prior->operand_index > 9U ||
            !core_asm_register_name_equal(
                operand_registers[prior->operand_index], register_name)) {
            continue;
        }
        alias_count += 1U;
        if (alias_count != 1U) {
            return false;
        }
        current_is_input =
            current->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        current_is_output =
            current->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        prior_is_input = prior->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        prior_is_output = prior->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (!((current_is_input && prior_is_output) ||
              (current_is_output && prior_is_input))) {
            return false;
        }
        output = current_is_output ? current : prior;
        if (output->early_clobber) {
            return false;
        }
    }
    return alias_count == 1U;
}

static bool core_structured_inline_asm_allocate(
    const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction,
    const char **operand_registers,
    bool *memory_operand,
    const char **scratch_register) {
    static const char *const output_preferences[] = {
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    static const char *const memory_preferences[] = {
        "t2", "t6", "t5", "t4", "t3", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    static const char *const input_preferences[] = {
        "t3", "t4", "t5", "t6", "t2", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    const MinicCoreInlineAsm *inline_asm;
    size_t operand_count;
    size_t binding_index;
    size_t clobber_index;

    if (function == NULL || instruction == NULL ||
        operand_registers == NULL || memory_operand == NULL || scratch_register == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    operand_count = instruction->value.structured_inline_asm.operand_count;
    if (operand_count == 0U || operand_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    for (binding_index = 0U; binding_index < 10U; ++binding_index) {
        operand_registers[binding_index] = NULL;
        memory_operand[binding_index] = false;
    }

    /* M172: explicit callee-saved clobbers are valid because the function
       frame now preserves the complete callee-saved bank. Unknown architectural
       register names still fail closed. */
    for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
         ++clobber_index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[clobber_index];
        if (clobber->name == NULL ||
            (!core_asm_register_is_caller_saved(clobber->name) &&
             !core_asm_register_is_callee_saved(clobber->name))) {
            return false;
        }
    }

    /* Reserve all fixed bindings before generic allocation so source order
       cannot accidentally steal a required architectural register. */
    for (binding_index = 0U; binding_index < operand_count; ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const MinicCoreFixedRegisterBinding *fixed_binding;
        if (!binding->has_fixed_register_binding) {
            continue;
        }
        if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
            binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            return false;
        }
        fixed_binding =
            core_fixed_register_binding(function, binding->fixed_register_binding_id);
        if (fixed_binding == NULL || !fixed_binding->is_local ||
            fixed_binding->register_name == NULL || fixed_binding->register_name_length == 0U ||
            core_inline_asm_clobbers_register(inline_asm, fixed_binding->register_name)) {
            return false;
        }
        if (core_asm_register_in_use(operand_registers, 10U, fixed_binding->register_name) &&
            !core_structured_fixed_phase_alias_safe(
                instruction, binding_index, operand_registers, fixed_binding->register_name)) {
            return false;
        }
        operand_registers[binding->operand_index] = fixed_binding->register_name;
    }

    for (binding_index = 0U; binding_index < operand_count; ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;
        const char *const *preferences;
        size_t preference_count;

        if (operand_registers[binding->operand_index] != NULL) {
            continue;
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            preferences = output_preferences;
            preference_count = sizeof(output_preferences) / sizeof(output_preferences[0]);
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            preferences = memory_preferences;
            preference_count = sizeof(memory_preferences) / sizeof(memory_preferences[0]);
            memory_operand[binding->operand_index] = true;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            preferences = input_preferences;
            preference_count = sizeof(input_preferences) / sizeof(input_preferences[0]);
            break;
        default:
            return false;
        }
        register_name = core_asm_choose_register(
            inline_asm, operand_registers, 10U, preferences, preference_count);
        if (register_name == NULL) {
            return false;
        }
        operand_registers[binding->operand_index] = register_name;
    }

    *scratch_register = NULL;
    for (binding_index = 0U;
         binding_index < sizeof(core_asm_caller_saved_registers) /
                             sizeof(core_asm_caller_saved_registers[0]);
         ++binding_index) {
        const char *candidate = core_asm_caller_saved_registers[binding_index].name;
        /* Scratch is used only before/after the asm, so an asm clobber is fine;
           it merely must not alias a live operand register. */
        if (!core_asm_register_in_use(operand_registers, 10U, candidate)) {
            *scratch_register = candidate;
            break;
        }
    }
    return *scratch_register != NULL;
}

/* M126A_GENERIC_STRUCTURED_ASM: capability is now role/resource based. */
static bool core_structured_inline_asm_supported(const MinicCoreFunction *function,
                                                 const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    const char *operand_registers[10] = {NULL};
    bool memory_operand[10] = {false};
    const char *scratch_register = NULL;
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
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type) ||
                (fixed_binding != NULL && !minic_type_equal(fixed_binding->type, value_type))) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (fixed_binding != NULL ||
                !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!core_scalar_type(function->values[binding->value].type) ||
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
    return core_structured_inline_asm_allocate(function,
                                                instruction,
                                                operand_registers,
                                                memory_operand,
                                                &scratch_register);
}

/* M85_RECORD_CALL_ARGUMENT: validate direct-call arguments against the
   shared RV64 ABI classifier. Core stores VALUE/OBJECT form only; register
   placement remains entirely target-owned. The first caller tier intentionally
   stays register-only, matching the pre-M85 Core call backend's no-stack scope. */
static bool core_direct_call_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        instruction->value.call.callee_id >= function->callee_count) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (callee->name == NULL || callee->name_length == 0U ||
        (!callee->is_variadic &&
         instruction->value.call.argument_count != callee->parameter_count) ||
        (callee->is_variadic &&
         instruction->value.call.argument_count < callee->parameter_count) ||
        instruction->value.call.argument_begin > function->call_argument_count ||
        instruction->value.call.argument_count >
            function->call_argument_count - instruction->value.call.argument_begin) {
        return false;
    }
    if (program == NULL) {
        if ((!minic_type_is_void(callee->return_type) &&
             !core_scalar_type(callee->return_type)) ||
            instruction->value.call.argument_count > 8U) {
            return false;
        }
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.call.argument_begin + argument_index];
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                return false;
            }
            if (argument_index < callee->parameter_count) {
                if (!core_scalar_type(callee->parameter_types[argument_index])) {
                    return false;
                }
            } else if (argument->value.value_id >= function->value_count ||
                       !core_scalar_type(function->values[argument->value.value_id].type)) {
                return false;
            }
        }
        return true;
    }
    /* M86_DIRECT_RECORD_CALL_RESULT: mirror the existing callee-side
       one/two-slot aggregate return ABI on direct call sites. */
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value)) {
        return false;
    }
    if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
        if (return_value.slot_count == 0U || return_value.slot_count > 2U ||
            !minic_type_is_record(callee->return_type) ||
            instruction->value.call.result_object >= function->object_count ||
            !minic_type_equal(
                function->objects[instruction->value.call.result_object].type,
                callee->return_type)) {
            return false;
        }
    } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        if (!minic_type_is_record(callee->return_type) || return_value.storage_size <= 16U ||
            return_value.slot_count != 1U ||
            instruction->value.call.result_object >= function->object_count ||
            !minic_type_equal(
                function->objects[instruction->value.call.result_object].type,
                callee->return_type)) {
            return false;
        }
    } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
               return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
               !(return_value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                 minic_type_is_double(callee->return_type))) {
        return false;
    }
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter;

        is_fixed_parameter = argument_index < callee->parameter_count;
        if (is_fixed_parameter) {
            argument_type = callee->parameter_types[argument_index];
        } else {
            if (!callee->is_variadic) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (argument->value.value_id >= function->value_count) {
                    return false;
                }
                argument_type = function->values[argument->value.value_id].type;
                if (!core_scalar_type(argument_type)) {
                    return false;
                }
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (argument->value.object_id >= function->object_count) {
                    return false;
                }
                argument_type = function->objects[argument->value.object_id].type;
                if (!minic_type_is_record(argument_type)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location)) {
            return false;
        }
        if (core_scalar_type(argument_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
            if (location.floating_register_count != 0U) {
                if (!is_fixed_parameter ||
                    location.value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                    !minic_type_is_double(argument_type) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
            } else {
                if ((location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                     !(location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                       !is_fixed_parameter && minic_type_is_double(argument_type))) ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return false;
                }
            }
        } else if (minic_type_is_record(argument_type)) {
            MinicCoreObjectId object_id;

            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                return false;
            }
            object_id = argument->value.object_id;
            if (object_id >= function->object_count ||
                !minic_type_equal(function->objects[object_id].type, argument_type)) {
                return false;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                if (location.value.slot_count != 0U || location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                    location.value.slot_count !=
                        location.integer_register_count + location.stack_slot_count ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.value.slot_count != 1U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

/* M151_INDIRECT_CALL_BATCH_OWNER: validate indirect arguments through the
   same register-only RV64 ABI classifier used by direct calls. */
static bool core_indirect_call_supported(const MinicC0Program *program,
                                         const MinicCoreFunction *function,
                                         const MinicCoreInstruction *instruction) {
    const MinicCoreCallSignature *signature;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    MinicType function_type;
    size_t argument_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
        instruction->value.indirect_call.signature_id >= function->call_signature_count ||
        instruction->value.indirect_call.callee >= function->value_count ||
        instruction->value.indirect_call.argument_begin > function->call_argument_count ||
        instruction->value.indirect_call.argument_count >
            function->call_argument_count - instruction->value.indirect_call.argument_begin ||
        !minic_type_pointee(
            function->values[instruction->value.indirect_call.callee].type, &function_type) ||
        !minic_type_is_function(function_type)) {
        return false;
    }
    signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
    if (function_type.function_type_id != signature->function_type_id ||
        (!signature->is_variadic &&
         instruction->value.indirect_call.argument_count != signature->parameter_count) ||
        (signature->is_variadic &&
         instruction->value.indirect_call.argument_count < signature->parameter_count)) {
        return false;
    }
    if (program == NULL) {
        if ((!minic_type_is_void(signature->return_type) &&
             !core_scalar_type(signature->return_type)) ||
            instruction->value.indirect_call.argument_count > 8U) {
            return false;
        }
        for (argument_index = 0U;
             argument_index < instruction->value.indirect_call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
            if (argument_index < signature->parameter_count) {
                if (!core_scalar_type(signature->parameter_types[argument_index])) {
                    return false;
                }
            } else if (!signature->is_variadic ||
                       !core_scalar_type(function->values[argument->value.value_id].type)) {
                return false;
            }
        }
        return true;
    }
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, signature->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER)) {
        return false;
    }
    for (argument_index = 0U;
         argument_index < instruction->value.indirect_call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.indirect_call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter = argument_index < signature->parameter_count;

        if (is_fixed_parameter) {
            argument_type = signature->parameter_types[argument_index];
        } else {
            if (!signature->is_variadic ||
                argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
            argument_type = function->values[argument->value.value_id].type;
            if (!core_scalar_type(argument_type)) {
                return false;
            }
        }
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location)) {
            return false;
        }
        if (core_scalar_type(argument_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
            if (location.floating_register_count != 0U) {
                if (!is_fixed_parameter ||
                    location.value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                    !minic_type_is_double(argument_type) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
            } else {
                if ((location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                     !(location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                       !is_fixed_parameter && minic_type_is_double(argument_type))) ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return false;
                }
            }
        } else if (is_fixed_parameter && minic_type_is_record(argument_type)) {
            MinicCoreObjectId object_id;

            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                return false;
            }
            object_id = argument->value.object_id;
            if (object_id >= function->object_count ||
                !minic_type_equal(function->objects[object_id].type, argument_type)) {
                return false;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                if (location.value.slot_count != 0U || location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                    location.value.slot_count !=
                        location.integer_register_count + location.stack_slot_count ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.value.slot_count != 1U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    if (function == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
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
        return core_call_frame_address_supported(instruction);
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t fixed_stack_slots;
        size_t integer_parameter_count;

        return program != NULL && instruction->result < function->value_count &&
               minic_type_equal(function->values[instruction->result].type, instruction->type) &&
               minic_type_is_pointer(instruction->type) &&
               core_variadic_fixed_prefix(
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
        return core_record_load_supported(program, function, instruction, NULL);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return core_record_copy_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = core_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        return binding != NULL && binding->register_name != NULL &&
               binding->register_name_length != 0U && core_scalar_type(binding->type) &&
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

        return core_integer_overflow_supported(
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
        return core_direct_call_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return core_indirect_call_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return core_field_address_supported(instruction, NULL);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return core_scalar_bitcast_supported(program, function, instruction);
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
            !core_scalar_type(function->return_type)) {
            return core_rv64_capability_reject(function, "legacy-return", 0U, -1);
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            if (!core_scalar_type(function->parameter_types[index])) {
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
        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                   !(return_value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                     minic_type_is_double(function->return_type))) {
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
                if (!minic_type_is_double(function->parameter_types[index]) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return core_rv64_capability_reject(function, "parameter-float", index, -1);
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
        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type) &&
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
        if (!core_scalar_type(function->values[index].type)) {
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

static bool core_parameter_location(const MinicC0Program *program,
                                    const MinicCoreFunction *function,
                                    size_t parameter_index,
                                    MinicRiscv64AbiArgumentLocation *location) {
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t index;

    if (program == NULL || function == NULL || location == NULL ||
        parameter_index >= function->parameter_count ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, function->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (index = 0U; index <= parameter_index; ++index) {
        MinicRiscv64AbiArgumentLocation current;

        if (!minic_riscv64_abi_place_argument(
                program, function->parameter_types[index], true, &cursor, &current)) {
            return false;
        }
        if (index == parameter_index) {
            *location = current;
        }
    }
    return true;
}

static bool emit_incoming_stack_load64(FILE *file,
                                               const MinicRiscv64CoreFrame *frame,
                                               const char *destination_register,
                                               size_t stack_slot) {
    size_t byte_offset;

    if (file == NULL || frame == NULL || destination_register == NULL ||
        stack_slot > SIZE_MAX / 8U) {
        return false;
    }
    byte_offset = stack_slot * 8U;
    if (!frame->has_dynamic_stack_alignment) {
        if (byte_offset > SIZE_MAX - frame->frame_size) {
            return false;
        }
        return minic_riscv64_emit_sp_load64(
            file, destination_register, frame->frame_size + byte_offset);
    }
    if (!minic_riscv64_emit_sp_load64(file, "t3", frame->entry_sp_offset)) {
        return false;
    }
    if (byte_offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(t3)\n", destination_register, byte_offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add t3, t3, t2\n"
                   "  ld %s, 0(t3)\n",
                   byte_offset,
                   destination_register) >= 0;
}

static bool emit_parameter(FILE *file,
                           const MinicC0Program *program,
                           const MinicCoreFunction *function,
                           const MinicRiscv64CoreFrame *frame,
                           const MinicCoreInstruction *instruction) {
    size_t parameter_index;

    parameter_index = instruction->value.parameter_index;
    if (parameter_index >= function->parameter_count) {
        return false;
    }
    if (program != NULL) {
        MinicRiscv64AbiArgumentLocation location;

        if (!core_parameter_location(program, function, parameter_index, &location)) {
            return false;
        }
        if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
            minic_type_is_double(instruction->type)) {
            if (location.floating_register_count != 1U ||
                location.floating_register_begin >= 8U ||
                location.integer_register_count != 0U ||
                location.stack_slot_count != 0U ||
                fprintf(file,
                        "  fmv.x.d t0, %s\n",
                        minic_core_rv64_floating_argument_registers[
                            location.floating_register_begin]) < 0) {
                return false;
            }
        } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER &&
                   location.floating_register_count == 0U) {
            if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
                location.integer_register_begin < 8U) {
                if (fprintf(file,
                            "  mv t0, %s\n",
                            minic_core_rv64_argument_registers[
                                location.integer_register_begin]) < 0) {
                    return false;
                }
            } else if (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U) {
                if (!emit_incoming_stack_load64(
                        file, frame, "t0", location.stack_slot_begin)) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else if (parameter_index < 8U) {
        if (fprintf(file, "  mv t0, %s\n", minic_core_rv64_argument_registers[parameter_index]) <
            0) {
            return false;
        }
    } else {
        size_t stack_slot;

        stack_slot = parameter_index - 8U;
        if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
            return false;
        }
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "t0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "t0");
}

static bool
emit_sp_load_chunk(FILE *file, const char *destination_register, size_t offset, size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || destination_register == NULL || size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return minic_riscv64_emit_sp_load64(file, destination_register, offset);
    }
    opcode = size == 4U ? "lwu" : size == 2U ? "lhu" : size == 1U ? "lbu" : NULL;
    if (opcode != NULL) {
        if (offset <= 2047U) {
            return fprintf(file, "  %s %s, %zu(sp)\n", opcode, destination_register, offset) >= 0;
        }
        return emit_sp_address(file, "t3", offset) &&
               fprintf(file, "  %s %s, 0(t3)\n", opcode, destination_register) >= 0;
    }
    if (fprintf(file, "  li %s, 0\n", destination_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < size; ++byte_index) {
        size_t byte_offset;

        if (offset > SIZE_MAX - byte_index) {
            return false;
        }
        byte_offset = offset + byte_index;
        if (byte_offset <= 2047U) {
            if (fprintf(file, "  lbu t1, %zu(sp)\n", byte_offset) < 0) {
                return false;
            }
        } else if (!emit_sp_address(file, "t3", byte_offset) ||
                   fprintf(file, "  lbu t1, 0(t3)\n") < 0) {
            return false;
        }
        if (byte_index != 0U && fprintf(file, "  slli t1, t1, %zu\n", byte_index * 8U) < 0) {
            return false;
        }
        if (fprintf(file, "  or %s, %s, t1\n", destination_register, destination_register) < 0) {
            return false;
        }
    }
    return true;
}

static bool
emit_sp_store_chunk(FILE *file, const char *source_register, size_t offset, size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || source_register == NULL || size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return minic_riscv64_emit_sp_store64(file, source_register, offset);
    }
    opcode = size == 4U ? "sw" : size == 2U ? "sh" : size == 1U ? "sb" : NULL;
    if (opcode != NULL) {
        if (offset <= 2047U) {
            return fprintf(file, "  %s %s, %zu(sp)\n", opcode, source_register, offset) >= 0;
        }
        return emit_sp_address(file, "t3", offset) &&
               fprintf(file, "  %s %s, 0(t3)\n", opcode, source_register) >= 0;
    }
    if (fprintf(file, "  mv t1, %s\n", source_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < size; ++byte_index) {
        size_t byte_offset;

        if (offset > SIZE_MAX - byte_index) {
            return false;
        }
        byte_offset = offset + byte_index;
        if (byte_offset <= 2047U) {
            if (fprintf(file, "  sb t1, %zu(sp)\n", byte_offset) < 0) {
                return false;
            }
        } else if (!emit_sp_address(file, "t3", byte_offset) ||
                   fprintf(file, "  sb t1, 0(t3)\n") < 0) {
            return false;
        }
        if (byte_index + 1U < size && fprintf(file, "  srli t1, t1, 8\n") < 0) {
            return false;
        }
    }
    return true;
}

static bool emit_parameter_object(FILE *file,
                                  const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  const MinicRiscv64CoreFrame *frame,
                                  const MinicCoreInstruction *instruction) {
    MinicRiscv64AbiArgumentLocation location;
    MinicCoreObjectId object_id;
    MinicType object_value_type;
    size_t object_offset;
    size_t chunk_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        return false;
    }
    object_id = instruction->value.parameter_object.object_id;
    if (!core_parameter_location(
            program, function, instruction->value.parameter_object.parameter_index, &location) ||
        object_id >= function->object_count ||
        !minic_type_unqualified(function->objects[object_id].type, &object_value_type) ||
        !minic_type_equal(
            object_value_type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, frame, object_id, &object_offset)) {
        return false;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        return location.value.slot_count == 0U && location.integer_register_count == 0U &&
               location.stack_slot_count == 0U;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        size_t copied;

        if (location.value.storage_size == 0U || location.value.slot_count != 1U ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
            size_t register_index = location.integer_register_begin;

            if (register_index >= 8U ||
                fprintf(file,
                        "  mv t1, %s\n",
                        minic_core_rv64_argument_registers[register_index]) < 0) {
                return false;
            }
        } else if (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U) {
            if (!emit_incoming_stack_load64(
                    file, frame, "t1", location.stack_slot_begin)) {
                return false;
            }
        } else {
            return false;
        }
        if (!emit_sp_address(file, "t0", object_offset)) {
            return false;
        }
        copied = 0U;
        while (copied < location.value.storage_size) {
            size_t chunk = location.value.storage_size - copied;
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
            if (copied < location.value.storage_size &&
                fprintf(file,
                        "  addi t0, t0, 2047\n"
                        "  addi t0, t0, 1\n"
                        "  addi t1, t1, 2047\n"
                        "  addi t1, t1, 1\n") < 0) {
                return false;
            }
        }
        return true;
    }
    if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count) {
        return false;
    }
    for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
        const char *source_register;
        size_t chunk_offset;
        size_t chunk_size;

        source_register = "t0";
        if (chunk_index < location.integer_register_count) {
            size_t register_index;

            register_index = location.integer_register_begin + chunk_index;
            if (register_index >= 8U) {
                return false;
            }
            source_register = minic_core_rv64_argument_registers[register_index];
        } else {
            size_t stack_slot;

            stack_slot =
                location.stack_slot_begin + (chunk_index - location.integer_register_count);
            if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
                return false;
            }
        }
        chunk_offset = chunk_index * 8U;
        if (chunk_offset >= location.value.storage_size ||
            object_offset > SIZE_MAX - chunk_offset) {
            return false;
        }
        chunk_size = location.value.storage_size - chunk_offset;
        if (chunk_size > 8U) {
            chunk_size = 8U;
        }
        if (!emit_sp_store_chunk(file, source_register, object_offset + chunk_offset, chunk_size)) {
            return false;
        }
    }
    return true;
}

static bool emit_call(FILE *file,
                      const MinicC0Program *program,
                      const MinicCoreFunction *function,
                      const MinicRiscv64CoreFrame *frame,
                      const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        !core_direct_call_supported(program, function, instruction)) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value)) {
        return false;
    }
    if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        size_t result_offset;

        if (instruction->value.call.result_object >= function->object_count ||
            !core_object_offset(
                program, function, frame, instruction->value.call.result_object, &result_offset) ||
            !emit_sp_address(file, "a0", result_offset)) {
            return false;
        }
    }
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter;

        is_fixed_parameter = argument_index < callee->parameter_count;
        if (is_fixed_parameter) {
            argument_type = callee->parameter_types[argument_index];
        } else {
            if (!callee->is_variadic) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (argument->value.value_id >= function->value_count) {
                    return false;
                }
                argument_type = function->values[argument->value.value_id].type;
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (argument->value.object_id >= function->object_count) {
                    return false;
                }
                argument_type = function->objects[argument->value.object_id].type;
            } else {
                return false;
            }
        }
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location)) {
            return false;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (location.floating_register_count != 0U) {
                if (!is_fixed_parameter ||
                    location.value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                    !minic_type_is_double(argument_type) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U ||
                    !load_core_value(file, frame, argument->value.value_id, "t0") ||
                    fprintf(file,
                            "  fmv.d.x %s, t0\n",
                            minic_core_rv64_floating_argument_registers[
                                location.floating_register_begin]) < 0) {
                    return false;
                }
            } else if (location.integer_register_count == 1U &&
                       location.stack_slot_count == 0U) {
                if (location.integer_register_begin >= 8U ||
                    !load_core_value(
                        file,
                        frame,
                        argument->value.value_id,
                        minic_core_rv64_argument_registers[location.integer_register_begin])) {
                    return false;
                }
            } else if (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U) {
                size_t outgoing_offset;

                if (location.stack_slot_begin > SIZE_MAX / 8U ||
                    !load_core_value(file, frame, argument->value.value_id, "t0")) {
                    return false;
                }
                outgoing_offset = location.stack_slot_begin * 8U;
                if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                    return false;
                }
            } else {
                return false;
            }
            continue;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            size_t chunk_index;
            size_t object_offset;

            if (!core_object_offset(
                    program, function, frame, argument->value.object_id, &object_offset)) {
                return false;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                if (location.value.slot_count != 0U || location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
                continue;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
                    if (location.integer_register_begin >= 8U ||
                        !emit_sp_address(file,
                                         minic_core_rv64_argument_registers[
                                             location.integer_register_begin],
                                         object_offset)) {
                        return false;
                    }
                } else if (location.integer_register_count == 0U &&
                           location.stack_slot_count == 1U) {
                    size_t outgoing_offset;
                    if (location.stack_slot_begin > SIZE_MAX / 8U ||
                        !emit_sp_address(file, "t0", object_offset)) {
                        return false;
                    }
                    outgoing_offset = location.stack_slot_begin * 8U;
                    if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                        return false;
                    }
                } else {
                    return false;
                }
                continue;
            }
            if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t chunk_offset = chunk_index * 8U;
                size_t chunk_size;

                if (chunk_offset >= location.value.storage_size ||
                    object_offset > SIZE_MAX - chunk_offset) {
                    return false;
                }
                chunk_size = location.value.storage_size - chunk_offset;
                if (chunk_size > 8U) {
                    chunk_size = 8U;
                }
                if (chunk_index < location.integer_register_count) {
                    size_t register_index = location.integer_register_begin + chunk_index;
                    if (register_index >= 8U ||
                        !emit_sp_load_chunk(file,
                                            minic_core_rv64_argument_registers[register_index],
                                            object_offset + chunk_offset,
                                            chunk_size)) {
                        return false;
                    }
                } else {
                    size_t stack_chunk = chunk_index - location.integer_register_count;
                    size_t stack_slot;
                    size_t outgoing_offset;
                    if (stack_chunk >= location.stack_slot_count ||
                        location.stack_slot_begin > SIZE_MAX - stack_chunk) {
                        return false;
                    }
                    stack_slot = location.stack_slot_begin + stack_chunk;
                    if (stack_slot > SIZE_MAX / 8U ||
                        !emit_sp_load_chunk(
                            file, "t0", object_offset + chunk_offset, chunk_size)) {
                        return false;
                    }
                    outgoing_offset = stack_slot * 8U;
                    if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                        return false;
                    }
                }
            }
            continue;
        }
        return false;
    }
    if (fprintf(file, "  call %s\n", callee->name) < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_record(instruction->type)) {
        size_t chunk_index;
        size_t object_offset;

        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            return return_value.storage_size > 16U && return_value.slot_count == 1U &&
                   instruction->value.call.result_object < function->object_count;
        }
        if (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            return_value.slot_count == 0U || return_value.slot_count > 2U ||
            instruction->value.call.result_object >= function->object_count ||
            !core_object_offset(
                program, function, frame, instruction->value.call.result_object, &object_offset)) {
            return false;
        }
        for (chunk_index = 0U; chunk_index < return_value.slot_count; ++chunk_index) {
            size_t chunk_offset = chunk_index * 8U;
            size_t chunk_size;
            const char *source_register =
                minic_core_rv64_argument_registers[chunk_index];

            if (chunk_offset >= return_value.storage_size ||
                object_offset > SIZE_MAX - chunk_offset) {
                return false;
            }
            chunk_size = return_value.storage_size - chunk_offset;
            if (chunk_size > 8U) {
                chunk_size = 8U;
            }
            if (!emit_sp_store_chunk(
                    file, source_register, object_offset + chunk_offset, chunk_size)) {
                return false;
            }
        }
        return true;
    }
    if (minic_type_is_double(instruction->type)) {
        if (return_value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
            fprintf(file, "  fmv.x.d a0, fa0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "a0");
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "a0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "a0");
}

static bool emit_indirect_call(FILE *file,
                               const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    const MinicCoreCallSignature *signature;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
        !core_indirect_call_supported(program, function, instruction)) {
        return false;
    }
    signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, signature->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (argument_index = 0U;
         argument_index < instruction->value.indirect_call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.indirect_call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter = argument_index < signature->parameter_count;

        if (is_fixed_parameter) {
            argument_type = signature->parameter_types[argument_index];
        } else {
            argument_type = function->values[argument->value.value_id].type;
        }
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location)) {
            return false;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (location.floating_register_count != 0U) {
                if (!is_fixed_parameter ||
                    location.value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                    !minic_type_is_double(argument_type) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U ||
                    !load_core_value(file, frame, argument->value.value_id, "t0") ||
                    fprintf(file,
                            "  fmv.d.x %s, t0\n",
                            minic_core_rv64_floating_argument_registers[
                                location.floating_register_begin]) < 0) {
                    return false;
                }
            } else if (location.integer_register_count == 1U &&
                       location.stack_slot_count == 0U) {
                if (location.integer_register_begin >= 8U ||
                    !load_core_value(
                        file,
                        frame,
                        argument->value.value_id,
                        minic_core_rv64_argument_registers[location.integer_register_begin])) {
                    return false;
                }
            } else if (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U) {
                size_t outgoing_offset;

                if (location.stack_slot_begin > SIZE_MAX / 8U ||
                    !load_core_value(file, frame, argument->value.value_id, "t0")) {
                    return false;
                }
                outgoing_offset = location.stack_slot_begin * 8U;
                if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                    return false;
                }
            } else {
                return false;
            }
            continue;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            size_t chunk_index;
            size_t object_offset;

            if (!is_fixed_parameter ||
                !core_object_offset(program, function, frame, argument->value.object_id, &object_offset)) {
                return false;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                if (location.value.slot_count != 0U || location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
                continue;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
                    if (location.integer_register_begin >= 8U ||
                        !emit_sp_address(file,
                                         minic_core_rv64_argument_registers[
                                             location.integer_register_begin],
                                         object_offset)) {
                        return false;
                    }
                } else if (location.integer_register_count == 0U &&
                           location.stack_slot_count == 1U) {
                    size_t outgoing_offset;

                    if (location.stack_slot_begin > SIZE_MAX / 8U ||
                        !emit_sp_address(file, "t0", object_offset)) {
                        return false;
                    }
                    outgoing_offset = location.stack_slot_begin * 8U;
                    if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                        return false;
                    }
                } else {
                    return false;
                }
                continue;
            }
            if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t chunk_offset = chunk_index * 8U;
                size_t chunk_size;

                if (chunk_offset >= location.value.storage_size ||
                    object_offset > SIZE_MAX - chunk_offset) {
                    return false;
                }
                chunk_size = location.value.storage_size - chunk_offset;
                if (chunk_size > 8U) {
                    chunk_size = 8U;
                }
                if (chunk_index < location.integer_register_count) {
                    size_t register_index = location.integer_register_begin + chunk_index;
                    if (register_index >= 8U ||
                        !emit_sp_load_chunk(file,
                                            minic_core_rv64_argument_registers[register_index],
                                            object_offset + chunk_offset,
                                            chunk_size)) {
                        return false;
                    }
                } else {
                    size_t stack_chunk = chunk_index - location.integer_register_count;
                    size_t stack_slot;
                    size_t outgoing_offset;

                    if (stack_chunk >= location.stack_slot_count ||
                        location.stack_slot_begin > SIZE_MAX - stack_chunk) {
                        return false;
                    }
                    stack_slot = location.stack_slot_begin + stack_chunk;
                    if (stack_slot > SIZE_MAX / 8U ||
                        !emit_sp_load_chunk(
                            file, "t0", object_offset + chunk_offset, chunk_size)) {
                        return false;
                    }
                    outgoing_offset = stack_slot * 8U;
                    if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                        return false;
                    }
                }
            }
            continue;
        }
        return false;
    }
    if (!load_core_value(file, frame, instruction->value.indirect_call.callee, "t0") ||
        fprintf(file, "  jalr ra, t0, 0\n") < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "a0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "a0");
}

static bool emit_field_address(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    size_t field_offset;

    if (!core_field_address_supported(instruction, &field_offset) ||
        !load_core_value(file, frame, instruction->value.field_address.base, "t0")) {
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
    return store_core_value(file, frame, instruction->result, "t0");
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
        if (symbol_name == NULL || symbol_name[0] == '\0' ||
            inline_asm->goto_target >= function->block_count ||
            fprintf(file,
                    "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                    "  .extern __minic_deferred_asm_immediate_%zu_0\n"
                    "  ",
                    inline_asm->source_inline_asm_id) < 0) {
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
                if (fprintf(file,
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
    return store_core_value(file, frame, instruction->result, "t0");
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
        !load_core_value(
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
    return store_core_value(file, frame, instruction->result, "t0");
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
        !load_core_value(
            file,
            frame,
            instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
            "t0") ||
        !load_core_value(
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
    return store_core_value(file, frame, instruction->result, "t1");
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
        !load_core_value(file, frame, instruction->value.scalar_input_inline_asm.operand, "t0")) {
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
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    const char *operand_registers[10] = {NULL};
    bool memory_operand[10] = {false};
    const char *scratch_register = NULL;
    size_t binding_index;
    size_t index;

    if (file == NULL || program == NULL || frame == NULL ||
        !core_structured_inline_asm_supported(function, instruction) ||
        !core_structured_inline_asm_allocate(function,
                                              instruction,
                                              operand_registers,
                                              memory_operand,
                                              &scratch_register)) {
        return false;
    }
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
            if (!load_core_value(file, frame, binding->value, scratch_register) ||
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
            if (!load_core_value(file, frame, binding->value, register_name)) {
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
            !load_core_value(file, frame, binding->value, scratch_register) ||
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
                             const MinicCoreInstruction *instruction) {
    size_t object_offset;

    if (file == NULL || function == NULL || frame == NULL || symbol_name == NULL || instruction == NULL ||
        !core_instruction_supported(program, function, instruction)) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (fprintf(file, "  li t0, %" PRId64 "\n", instruction->value.integer_value) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (!minic_type_is_double(instruction->type) ||
            fprintf(file, "  li t0, 0x%016" PRIx64 "\n", instruction->value.floating_bits) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
        const char *opcode;

        if (!minic_type_is_double(instruction->type)) {
            return false;
        }
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
            opcode = "fadd.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
            opcode = "fsub.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
            opcode = "fmul.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
            opcode = "fdiv.d";
            break;
        default:
            return false;
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fmv.d.x ft1, t1\n"
                    "  %s ft0, ft0, ft1\n"
                    "  fmv.x.d t0, ft0\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
        const char *opcode;

        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !minic_type_is_double(function->values[instruction->value.binary.left].type) ||
            !minic_type_equal(function->values[instruction->value.binary.left].type,
                              function->values[instruction->value.binary.right].type)) {
            return false;
        }
        opcode = instruction->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                     ? "feq.d"
                 : instruction->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                     ? "flt.d"
                     : "fle.d";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fmv.d.x ft1, t1\n"
                    "  %s t0, ft0, ft1\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  add t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sub t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        if (minic_type_is_int128_integer(instruction->type)) {
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_int128_integer(function->values[right].type) ||
                !load_core_int128_value(file, frame, left, "t0", "t1") ||
                !load_core_int128_value(file, frame, right, "t2", "t3") ||
                fprintf(file,
                        "  mulhu t4, t0, t2\n"
                        "  mul t5, t1, t2\n"
                        "  add t4, t4, t5\n"
                        "  mul t5, t0, t3\n"
                        "  add t4, t4, t5\n"
                        "  mul t0, t0, t2\n") < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t4");
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  mul t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_core_function_effective_integer_type(function, instruction->type, &effective_type)) {
            return false;
        }
        if (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE) {
            opcode = minic_type_is_unsigned_integer(effective_type) ? "divu" : "div";
        } else {
            opcode = minic_type_is_unsigned_integer(effective_type) ? "remu" : "rem";
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  and t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  or t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sll t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_core_function_effective_integer_type(function, instruction->type, &effective_type)) {
            return false;
        }
        if (minic_type_is_int128_integer(instruction->type)) {
            bool is_signed;
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_integer(function->values[right].type) ||
                minic_type_is_int128_integer(function->values[right].type) ||
                !core_integer_type_is_signed(function, instruction->type, &is_signed) ||
                !load_core_int128_value(file, frame, left, "t0", "t1") ||
                !load_core_value(file, frame, right, "t2") ||
                fprintf(file,
                        "  beqz t2, .L%s_core_i128_shr_done_%" PRIu32 "\n"
                        "  li t3, 64\n"
                        "  bgeu t2, t3, .L%s_core_i128_shr_ge64_%" PRIu32 "\n"
                        "  neg t3, t2\n"
                        "  sll t4, t1, t3\n"
                        "  srl t0, t0, t2\n"
                        "  or t0, t0, t4\n"
                        "  %s t1, t1, t2\n"
                        "  j .L%s_core_i128_shr_done_%" PRIu32 "\n"
                        ".L%s_core_i128_shr_ge64_%" PRIu32 ":\n"
                        "  addi t2, t2, -64\n"
                        "  %s t0, t1, t2\n"
                        "  %s\n"
                        ".L%s_core_i128_shr_done_%" PRIu32 ":\n",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        is_signed ? "  srai t1, t1, 63" : "  li t1, 0",
                        symbol_name,
                        instruction->result) < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t1");
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "srl" : "sra";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS: {
        MinicType effective_type;
        MinicType operand_type;
        const char *opcode;

        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count) {
            return false;
        }
        operand_type = function->values[instruction->value.binary.left].type;
        if (!minic_type_is_integer(operand_type) ||
            !minic_type_equal(operand_type,
                              function->values[instruction->value.binary.right].type) ||
            !minic_core_function_effective_integer_type(function, operand_type, &effective_type)) {
            return false;
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "sltu" : "slt";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !minic_type_is_pointer(function->values[instruction->value.binary.left].type) ||
            !minic_type_equal(function->values[instruction->value.binary.left].type,
                              function->values[instruction->value.binary.right].type) ||
            !load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sltu t0, t0, t1\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n  seqz t0, t0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType left_type;
        MinicType result_type;
        MinicType right_type;
        size_t result_size;
        bool is_unsigned;

        if (!core_integer_overflow_supported(
                program, function, instruction, &result_type, &result_size, &is_unsigned) ||
            !load_core_value(file, frame, instruction->value.integer_overflow.left, "t0") ||
            !load_core_value(file, frame, instruction->value.integer_overflow.right, "t1") ||
            !load_core_value(
                file, frame, instruction->value.integer_overflow.result_address, "t3")) {
            return false;
        }
        left_type = function->values[instruction->value.integer_overflow.left].type;
        right_type = function->values[instruction->value.integer_overflow.right].type;
        if ((!minic_type_equal(left_type, result_type) ||
             !minic_type_equal(right_type, result_type)) &&
            !(core_integer_type_range_fits(program, function, left_type, result_type) &&
              core_integer_type_range_fits(program, function, right_type, result_type)) &&
            !core_integer_overflow_xlen_scratch_exact(
                program, left_type, right_type, result_size)) {
            const char *signed_register;
            const char *unsigned_register;
            uint64_t maximum;

            if (is_unsigned || result_size >= 8U) {
                return false;
            }
            if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (fprintf(file, "  mul t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0 ||
                    !minic_riscv64_emit_scalar_store_for_program(
                        file, program, result_type, "t2", "t3")) {
                    return false;
                }
                return store_core_value(file, frame, instruction->result, "t4");
            }
            if (instruction->value.integer_overflow.operator_kind !=
                MINIC_CORE_INTEGER_OVERFLOW_ADD) {
                return false;
            }
            signed_register = minic_type_equal(left_type, result_type) ? "t0" : "t1";
            unsigned_register = minic_type_equal(left_type, result_type) ? "t1" : "t0";
            maximum = (UINT64_C(1) << (result_size * 8U - 1U)) - UINT64_C(1);
            if (fprintf(file,
                        "  li t5, %" PRIu64 "\n"
                        "  sub t5, t5, %s\n"
                        "  sltu t4, t5, %s\n"
                        "  add t2, t0, t1\n",
                        maximum,
                        signed_register,
                        unsigned_register) < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                !minic_riscv64_emit_scalar_store_for_program(
                    file, program, result_type, "t2", "t3")) {
                return false;
            }
            return store_core_value(file, frame, instruction->result, "t4");
        }

        if (instruction->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD) {
            if (result_size < 8U) {
                if (fprintf(file, "  add t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  add t2, t0, t1\n"
                            "  sltu t4, t2, t0\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  add t2, t0, t1\n"
                               "  xor t4, t0, t1\n"
                               "  xori t4, t4, -1\n"
                               "  xor t5, t0, t2\n"
                               "  and t4, t4, t5\n"
                               "  srli t4, t4, 63\n") < 0) {
                return false;
            }
        } else if (instruction->value.integer_overflow.operator_kind ==
                   MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
            if (result_size < 8U) {
                if (fprintf(file, "  sub t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  sub t2, t0, t1\n"
                            "  sltu t4, t0, t1\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  sub t2, t0, t1\n"
                               "  xor t4, t0, t1\n"
                               "  xor t5, t0, t2\n"
                               "  and t4, t4, t5\n"
                               "  srli t4, t4, 63\n") < 0) {
                return false;
            }
        } else if (result_size < 8U) {
            if (fprintf(file, "  mul t2, t0, t1\n  mv t4, t2\n") < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                return false;
            }
        } else if (is_unsigned) {
            if (fprintf(file,
                        "  mul t2, t0, t1\n"
                        "  mulhu t4, t0, t1\n"
                        "  snez t4, t4\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  mul t2, t0, t1\n"
                           "  mulh t4, t0, t1\n"
                           "  srai t5, t2, 63\n"
                           "  xor t4, t4, t5\n"
                           "  snez t4, t4\n") < 0) {
            return false;
        }
        if (!minic_riscv64_emit_scalar_store_for_program(file, program, result_type, "t2", "t3")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t4");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;

        if (operand >= function->value_count) {
            return false;
        }
        source_type = function->values[operand].type;
        if (minic_type_is_int128_integer(instruction->type)) {
            if (minic_type_is_int128_integer(source_type)) {
                if (!load_core_int128_value(file, frame, operand, "t0", "t1")) {
                    return false;
                }
            } else {
                bool source_signed;

                if (!minic_type_is_integer(source_type) ||
                    !load_core_value(file, frame, operand, "t0") ||
                    !core_integer_type_is_signed(function, source_type, &source_signed) ||
                    fprintf(file,
                            source_signed ? "  srai t1, t0, 63\n" : "  li t1, 0\n") < 0) {
                    return false;
                }
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t1");
        }
        if (minic_type_is_int128_integer(source_type)) {
            if (!load_core_int128_value(file, frame, operand, "t0", "t1") ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, instruction->type, "t0")) {
                return false;
            }
            return store_core_value(file, frame, instruction->result, "t0");
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !core_effective_integer_type(function, function->values[operand].type, &source_type) ||
            minic_type_is_int128_integer(source_type) ||
            !minic_type_is_double(instruction->type)) {
            return false;
        }
        if (minic_type_is_long_integer(source_type)) {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.lu" : "fcvt.d.l";
        } else {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.wu" : "fcvt.d.w";
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  %s ft0, t0\n"
                    "  fmv.x.d t1, ft0\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;

        if (operand >= function->value_count ||
            !minic_type_is_float(function->values[operand].type) ||
            !minic_type_is_double(instruction->type) ||
            !load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.w.x ft0, t0\n"
                    "  fcvt.d.s ft1, ft0\n"
                    "  fmv.x.d t1, ft1\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
        MinicCoreValueId operand = instruction->value.operand;

        if (operand >= function->value_count ||
            !minic_type_is_double(function->values[operand].type) ||
            !minic_type_is_float(instruction->type) ||
            !load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fcvt.s.d ft1, ft0\n"
                    "  fmv.x.w t1, ft1\n"
                    "  slli t1, t1, 32\n"
                    "  srli t1, t1, 32\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType target_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !minic_type_is_double(function->values[operand].type) ||
            !core_effective_integer_type(function, instruction->type, &target_type) ||
            minic_type_is_int128_integer(target_type)) {
            return false;
        }
        if (minic_type_is_long_integer(target_type)) {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.lu.d" : "fcvt.l.d";
        } else {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.wu.d" : "fcvt.w.d";
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  %s t1, ft0, rtz\n",
                    opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t1")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        if (!load_core_value(file, frame, instruction->value.operand, "t0")) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fsgnjn.d ft0, ft0, ft0\n"
                    "  fmv.x.d t0, ft0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  neg t0, t0\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  xori t0, t0, -1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ: {
        unsigned int width;

        if (!core_unsigned_integer_width(
                program, function, instruction->value.operand, &width) ||
            !load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  li t1, 0\n"
                    "  li t2, 1\n"
                    "  slli t2, t2, %u\n"
                    ".L%s_core_clz_loop_%" PRIu32 ":\n"
                    "  and t3, t0, t2\n"
                    "  bnez t3, .L%s_core_clz_done_%" PRIu32 "\n"
                    "  addi t1, t1, 1\n"
                    "  srli t2, t2, 1\n"
                    "  bnez t2, .L%s_core_clz_loop_%" PRIu32 "\n"
                    ".L%s_core_clz_done_%" PRIu32 ":\n",
                    width - 1U,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        unsigned int width;

        if (!core_unsigned_integer_width(
                program, function, instruction->value.operand, &width) ||
            !load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  beqz t0, .L%s_core_ctz_zero_%" PRIu32 "\n"
                    "  li t1, 0\n"
                    ".L%s_core_ctz_loop_%" PRIu32 ":\n"
                    "  andi t2, t0, 1\n"
                    "  bnez t2, .L%s_core_ctz_done_%" PRIu32 "\n"
                    "  addi t1, t1, 1\n"
                    "  srli t0, t0, 1\n"
                    "  j .L%s_core_ctz_loop_%" PRIu32 "\n"
                    ".L%s_core_ctz_zero_%" PRIu32 ":\n"
                    "  li t1, %u\n"
                    ".L%s_core_ctz_done_%" PRIu32 ":\n",
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    width,
                    symbol_name, instruction->result) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  seqz t0, t0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        if (!load_core_value(file, frame, instruction->value.pointer_offset.base, "t0") ||
            !load_core_value(file, frame, instruction->value.pointer_offset.index, "t1")) {
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
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = core_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U || !core_scalar_type(binding->type) ||
            !minic_type_equal(binding->type, instruction->type) ||
            fprintf(file, "  mv t0, %s\n", binding->register_name) < 0) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        if (!core_call_frame_address_supported(instruction)) {
            return false;
        }
        if (instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN) {
            if (!frame->saves_return_address ||
                !minic_riscv64_emit_sp_load64(file, "t0", frame->return_address_offset)) {
                return false;
            }
        } else if (instruction->value.call_frame_address.kind ==
                   MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) {
            if (fprintf(file, "  mv t0, sp\n") < 0) {
                return false;
            }
        } else {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        if (!frame->has_variadic_argument_address) {
            return false;
        }
        if (frame->varargs_size != 0U) {
            if (!emit_sp_address(file, "t0", frame->varargs_offset)) {
                return false;
            }
        } else {
            size_t stack_byte_offset;

            if (frame->variadic_fixed_stack_slots > SIZE_MAX / 8U) {
                return false;
            }
            stack_byte_offset = frame->variadic_fixed_stack_slots * 8U;
            if (frame->has_dynamic_stack_alignment) {
                if (!minic_riscv64_emit_sp_load64(file, "t0", frame->entry_sp_offset)) {
                    return false;
                }
            } else {
                if (frame->frame_size <= 2047U) {
                    if (fprintf(file, "  addi t0, sp, %zu\n", frame->frame_size) < 0) {
                        return false;
                    }
                } else if (fprintf(file,
                                   "  li t0, %zu\n"
                                   "  add t0, sp, t0\n",
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
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return emit_parameter_object(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!core_object_offset(program, function, frame, instruction->value.object_id, &object_offset) ||
            !emit_sp_address(file, "t0", object_offset)) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        if (instruction->value.block_id >= function->block_count ||
            fprintf(file, "  la t0, .L%s_core_bb%" PRIu32 "\n", symbol_name, instruction->value.block_id) < 0) return false;
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (instruction->value.global_id >= function->global_count ||
            fprintf(file, "  la t0, %s\n", function->globals[instruction->value.global_id].name) <
                0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (instruction->value.function_symbol_id >= function->function_symbol_count ||
            fprintf(file,
                    "  la t0, %s\n",
                    function->function_symbols[instruction->value.function_symbol_id].name) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (minic_type_is_int128_integer(instruction->type)) {
            if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
                fprintf(file, "  ld t1, 0(t0)\n  ld t2, 8(t0)\n") < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t1", "t2");
        }
        if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_scalar_load_for_program(
                file, program, instruction->type, "t1", "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId stored_value;
        MinicType stored_type;

        stored_value = instruction->value.store.stored_value;
        if (stored_value >= function->value_count) {
            return false;
        }
        stored_type = function->values[stored_value].type;
        if (minic_type_is_int128_integer(stored_type)) {
            return load_core_value(file, frame, instruction->value.store.address, "t0") &&
                   load_core_int128_value(file, frame, stored_value, "t1", "t2") &&
                   fprintf(file, "  sd t1, 0(t0)\n  sd t2, 8(t0)\n") >= 0;
        }
        return load_core_value(file, frame, instruction->value.store.address, "t0") &&
               load_core_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_scalar_store_for_program(file, program, stored_type, "t1", "t0");
    }
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const char *opcode;
        size_t destination_offset;
        size_t record_size;

        if (!core_record_load_supported(program, function, instruction, &record_size) ||
            !core_object_offset(program,
                                function,
                                frame,
                                instruction->value.record_load.destination_object,
                                &destination_offset) ||
            !load_core_value(
                file, frame, instruction->value.record_load.source_address, "t0")) {
            return false;
        }
        if (record_size <= 8U) {
            opcode = record_size == 8U ? "ld" : record_size == 4U ? "lwu" :
                     record_size == 2U ? "lhu" : "lbu";
            if (fprintf(file, "  %s t1, 0(t0)\n", opcode) < 0 ||
                !emit_sp_store_chunk(file, "t1", destination_offset, record_size)) {
                return false;
            }
            return true;
        }

        /* M162_CORE_RV64_RECORD_LOAD: materialize arbitrary non-empty records
           into the destination CoreObject without assuming source alignment.
           This mirrors RECORD_COPY's byte-safe O0 fallback. */
        if (!emit_sp_address(file, "t1", destination_offset)) {
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

        if (!core_record_copy_supported(program, function, instruction) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    instruction->type,
                                    &copy_size,
                                    &alignment) ||
            !load_core_value(
                file, frame, instruction->value.record_copy.destination_address, "t0") ||
            !load_core_value(file, frame, instruction->value.record_copy.source_address, "t1")) {
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
        return emit_structured_inline_asm(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return emit_indirect_call(file, program, function, frame, instruction);
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
                !core_object_offset(program, function, frame, terminator->return_object, &object_offset) ||
                !emit_sp_address(file, "t0", object_offset)) {
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
                    !minic_riscv64_emit_sp_load64(
                        file, "t1", frame->hidden_result_pointer_offset)) {
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
        } else if (minic_type_is_double(function->return_type)) {
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !load_core_value(file, frame, terminator->return_value, "a0") ||
                fprintf(file, "  fmv.d.x fa0, a0\n") < 0) {
                return false;
            }
        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !load_core_value(file, frame, terminator->return_value, "a0")) {
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
        return load_core_value(file, frame, terminator->indirect_target, "t0") &&
               fprintf(file, "  jalr zero, t0, 0\n") >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        /* Keep the architectural conditional branch local, then use the same
           far-transfer sequence for both CFG successors. The numeric local
           label is intentionally reusable; 1f always resolves to the next
           instance emitted by this terminator. */
        return load_core_value(file, frame, terminator->conditional.condition, "t0") &&
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
    if (!core_frame_initialize(program, function, &frame)) {
        return false;
    }
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-frame state=end "
                      "function=%s frame_size=%zu\n",
                      symbol_name,
                      frame.frame_size);
        (void)fflush(stderr);
    }
    if (!minic_riscv64_emit_function_symbol_begin(file, symbol)) {
        return false;
    }
    if (frame.has_dynamic_stack_alignment) {
        if (fprintf(file, "  mv t0, sp\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.frame_size) ||
            fprintf(file,
                    "  li t1, -%zu\n"
                    "  and sp, sp, t1\n",
                    frame.stack_alignment) < 0 ||
            !minic_riscv64_emit_sp_store64(file, "t0", frame.entry_sp_offset)) {
            return false;
        }
    } else if (!minic_riscv64_emit_stack_allocate(file, frame.frame_size)) {
        return false;
    }
    if (frame.saves_return_address &&
        !minic_riscv64_emit_sp_store64(file, "ra", frame.return_address_offset)) {
        return false;
    }
    if (frame.has_hidden_result_pointer &&
        !minic_riscv64_emit_sp_store64(file, "a0", frame.hidden_result_pointer_offset)) {
        return false;
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        for (saved_index = 0U; saved_index < CORE_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset = frame.structured_asm_callee_saved_offset + saved_index * 8U;
            if (!minic_riscv64_emit_sp_store64(
                    file, core_asm_callee_saved_registers[saved_index], saved_offset)) {
                return false;
            }
        }
    }
    if (frame.has_variadic_argument_address) {
        size_t register_index;

        for (register_index = frame.integer_parameter_count; register_index < 8U;
             ++register_index) {
            size_t offset = frame.varargs_offset +
                            (register_index - frame.integer_parameter_count) * 8U;
            if (!minic_riscv64_emit_sp_store64(
                    file, minic_core_rv64_argument_registers[register_index], offset)) {
                return false;
            }
        }
    }
    if (!emit_far_jump_to_block(file, symbol_name, function->entry_block)) {
        return false;
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
        if (!emit_block_label(file, symbol_name, (MinicCoreBlockId)block_index)) {
            return false;
        }
        if (block->source_label_id != SIZE_MAX &&
            fprintf(file, ".Luser_%zu:\n", block->source_label_id) < 0) {
            return false;
        }
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            MinicCoreInstructionId instruction_id;

            instruction_id = block->instructions[instruction_index];
            if (instruction_id >= function->instruction_count ||
                !emit_instruction(file, program, function, &frame, symbol_name, &function->instructions[instruction_id])) {
                return false;
            }
        }
        if (!block->has_terminator ||
            !emit_terminator(file, program, function, &frame, symbol_name, &block->terminator)) {
            return false;
        }
    }
    if (fprintf(file, ".L%s_core_return:\n", symbol_name) < 0) {
        return false;
    }
    if (frame.saves_return_address &&
        !minic_riscv64_emit_sp_load64(file, "ra", frame.return_address_offset)) {
        return false;
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        for (saved_index = 0U; saved_index < CORE_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset = frame.structured_asm_callee_saved_offset + saved_index * 8U;
            if (!minic_riscv64_emit_sp_load64(
                    file, core_asm_callee_saved_registers[saved_index], saved_offset)) {
                return false;
            }
        }
    }
    if (frame.has_dynamic_stack_alignment) {
        if (!minic_riscv64_emit_sp_load64(file, "t0", frame.entry_sp_offset) ||
            fprintf(file, "  mv sp, t0\n") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_stack_release(file, frame.frame_size)) {
        return false;
    }
    if (fprintf(file, "  ret\n") < 0 ||
        !minic_riscv64_emit_function_symbol_end(file, symbol)) {
        return false;
    }
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
