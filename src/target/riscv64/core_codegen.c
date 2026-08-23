#include "target/riscv64/core_codegen.h"

#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"
#include "target/data_layout.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

typedef struct MinicRiscv64CoreFrame {
    size_t frame_size;
    size_t object_count;
    size_t value_count;
    size_t value_base_offset;
    size_t return_address_offset;
    bool saves_return_address;
} MinicRiscv64CoreFrame;

static bool core_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
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

static bool core_function_has_call(const MinicCoreFunction *function) {
    size_t instruction_index;

    if (function == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        if (function->instructions[instruction_index].kind == MINIC_CORE_INSTRUCTION_CALL) {
            return true;
        }
    }
    return false;
}

static bool core_frame_initialize(const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  MinicRiscv64CoreFrame *frame) {
    size_t object_index;
    size_t storage_size;

    if (function == NULL || frame == NULL) {
        return false;
    }
    storage_size = 0U;
    for (object_index = 0U; object_index < function->object_count; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U ||
            !align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;
    }
    if (!align_up(storage_size, 8U, &frame->value_base_offset) ||
        function->value_count > (SIZE_MAX - frame->value_base_offset) / 8U) {
        return false;
    }
    storage_size = frame->value_base_offset + function->value_count * 8U;
    frame->saves_return_address = core_function_has_call(function);
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }
    if (!align_up(storage_size, 16U, &frame->frame_size)) {
        return false;
    }
    frame->object_count = function->object_count;
    frame->value_count = function->value_count;
    return true;
}

static bool core_object_offset(const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               MinicCoreObjectId object_id,
                               size_t *offset) {
    size_t current_offset;
    size_t object_index;

    if (function == NULL || offset == NULL || object_id >= function->object_count) {
        return false;
    }
    current_offset = 0U;
    for (object_index = 0U; object_index <= (size_t)object_id; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U ||
            !align_up(current_offset, object_alignment, &current_offset)) {
            return false;
        }
        if (object_index == (size_t)object_id) {
            *offset = current_offset;
            return true;
        }
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
        (size_t)value_id > (SIZE_MAX - frame->value_base_offset) / 8U) {
        return false;
    }
    *offset = frame->value_base_offset + (size_t)value_id * 8U;
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

static bool core_field_address_supported(const MinicC0Program *program,
                                         const MinicCoreInstruction *instruction,
                                         size_t *field_offset) {
    const MinicRecord *record;
    const MinicRecordField *field;
    size_t offset;

    if (program == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_FIELD_ADDRESS) {
        return false;
    }
    record = minic_c0_program_record(program, instruction->value.field_address.record_id);
    field = minic_c0_record_field(record, instruction->value.field_address.field_index);
    if (record == NULL || field == NULL || field->is_bit_field ||
        !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                               program,
                                               record,
                                               instruction->value.field_address.field_index,
                                               &offset)) {
        return false;
    }
    if (field_offset != NULL) {
        *field_offset = offset;
    }
    return true;
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

static bool core_integer_overflow_supported(const MinicC0Program *program,
                                            const MinicCoreFunction *function,
                                            const MinicCoreInstruction *instruction,
                                            MinicType *result_type,
                                            size_t *result_size,
                                            bool *is_unsigned) {
    MinicType effective_result_type;
    MinicType pointee;
    size_t alignment;

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
        !minic_type_equal(function->values[instruction->value.integer_overflow.left].type,
                          pointee) ||
        !minic_type_equal(function->values[instruction->value.integer_overflow.right].type,
                          pointee) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, pointee, result_size, &alignment) ||
        *result_size == 0U || *result_size > 8U ||
        !minic_c0_type_effective_integer_type(program, pointee, &effective_result_type)) {
        return false;
    }
    (void)alignment;
    if (result_type != NULL) {
        *result_type = pointee;
    }
    if (is_unsigned != NULL) {
        *is_unsigned = minic_type_is_unsigned_integer(effective_result_type);
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
    return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
           inline_asm->is_volatile;
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

static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;

    if (function == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
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
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
        return true;
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicFixedRegisterBinding *binding;

        if (program == NULL) {
            return false;
        }
        binding = minic_c0_program_fixed_register_binding(
            program, instruction->value.fixed_register_binding_id);
        return binding != NULL && binding->register_name != NULL &&
               binding->register_name_length != 0U && core_scalar_type(binding->type) &&
               minic_type_equal(binding->type, instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        return instruction->value.global_id < function->global_count &&
               function->globals[instruction->value.global_id].name != NULL &&
               function->globals[instruction->value.global_id].name_length != 0U;
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
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_count > 8U) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        return callee->name != NULL && callee->name_length != 0U && callee->parameter_count <= 8U;
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return core_field_address_supported(program, instruction, NULL);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return core_scalar_bitcast_supported(program, function, instruction);
    }
    return false;
}

static bool core_function_can_emit_basic_v0(const MinicC0Program *program,
                                            const MinicCoreFunction *function) {
    size_t index;

    if (function == NULL || !minic_core_function_verify(function)) {
        return false;
    }
    if (program == NULL) {
        if (!minic_type_is_void(function->return_type) &&
            !core_scalar_type(function->return_type)) {
            return false;
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            if (!core_scalar_type(function->parameter_types[index])) {
                return false;
            }
        }
    } else {
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value) ||
            (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
             (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
              return_value.slot_count == 0U || return_value.slot_count > 2U))) {
            return false;
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            MinicRiscv64AbiArgumentLocation location;

            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location) ||
                (location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                 (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                  location.value.slot_count == 0U || location.value.slot_count > 2U))) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->object_count; ++index) {
        size_t object_size;
        size_t object_alignment;
        MinicType object_type;

        object_type = function->objects[index].type;
        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U) {
            return false;
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||
            !core_scalar_type(function->globals[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < function->value_count; ++index) {
        if (!core_scalar_type(function->values[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < function->instruction_count; ++index) {
        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            return false;
        }
    }
    return true;
}

bool minic_riscv64_core_function_can_emit_basic_v0(const MinicCoreFunction *function) {
    return core_function_can_emit_basic_v0(NULL, function);
}

bool minic_riscv64_core_function_can_emit_basic_v0_for_program(const MinicC0Program *program,
                                                               const MinicCoreFunction *function) {
    return program != NULL && core_function_can_emit_basic_v0(program, function);
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

static bool emit_parameter(FILE *file,
                           const MinicC0Program *program,
                           const MinicCoreFunction *function,
                           const MinicRiscv64CoreFrame *frame,
                           const MinicCoreInstruction *instruction) {
    size_t parameter_index;
    size_t incoming_offset;

    parameter_index = instruction->value.parameter_index;
    if (parameter_index >= function->parameter_count) {
        return false;
    }
    if (program != NULL) {
        MinicRiscv64AbiArgumentLocation location;

        if (!core_parameter_location(program, function, parameter_index, &location) ||
            location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
            location.integer_register_begin < 8U) {
            if (fprintf(file,
                        "  mv t0, %s\n",
                        minic_core_rv64_argument_registers[location.integer_register_begin]) < 0) {
                return false;
            }
        } else if (location.integer_register_count == 0U && location.stack_slot_count == 1U) {
            if (location.stack_slot_begin > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + location.stack_slot_begin * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
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
        if (stack_slot > (SIZE_MAX - frame->frame_size) / 8U) {
            return false;
        }
        incoming_offset = frame->frame_size + stack_slot * 8U;
        if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
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
    size_t object_offset;
    size_t chunk_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        return false;
    }
    object_id = instruction->value.parameter_object.object_id;
    if (!core_parameter_location(
            program, function, instruction->value.parameter_object.parameter_index, &location) ||
        location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count ||
        object_id >= function->object_count ||
        !minic_type_equal(
            function->objects[object_id].type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, object_id, &object_offset)) {
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
            size_t incoming_offset;

            stack_slot =
                location.stack_slot_begin + (chunk_index - location.integer_register_count);
            if (stack_slot > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + stack_slot * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
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
    size_t argument_index;
    size_t argument_offset;

    if (file == NULL || function == NULL || frame == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        !core_instruction_supported(NULL, function, instruction)) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        argument_offset = instruction->value.call.argument_begin + argument_index;
        if (argument_offset >= function->call_argument_count ||
            !load_core_value(file,
                             frame,
                             function->call_arguments[argument_offset],
                             minic_core_rv64_argument_registers[argument_index])) {
            return false;
        }
    }
    if (fprintf(file, "  call %s\n", callee->name) < 0) {
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
                               const MinicC0Program *program,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    size_t field_offset;

    if (!core_field_address_supported(program, instruction, &field_offset) ||
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
                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || !core_opaque_inline_asm_supported(function, instruction)) {
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

static bool emit_instruction(FILE *file,
                             const MinicC0Program *program,
                             const MinicCoreFunction *function,
                             const MinicRiscv64CoreFrame *frame,
                             const MinicCoreInstruction *instruction) {
    size_t object_offset;

    if (file == NULL || function == NULL || frame == NULL || instruction == NULL ||
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

        if (!minic_c0_type_effective_integer_type(program, instruction->type, &effective_type)) {
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

        if (!minic_c0_type_effective_integer_type(program, instruction->type, &effective_type)) {
            return false;
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
            !minic_c0_type_effective_integer_type(program, operand_type, &effective_type)) {
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
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n  seqz t0, t0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;
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
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
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
        if (fprintf(file, "  add t0, t0, t1\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicFixedRegisterBinding *binding;

        if (program == NULL) {
            return false;
        }
        binding = minic_c0_program_fixed_register_binding(
            program, instruction->value.fixed_register_binding_id);
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
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return emit_parameter_object(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!core_object_offset(program, function, instruction->value.object_id, &object_offset) ||
            !emit_sp_address(file, "t0", object_offset)) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (instruction->value.global_id >= function->global_count ||
            fprintf(file, "  la t0, %s\n", function->globals[instruction->value.global_id].name) <
                0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_LOAD:
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
        return load_core_value(file, frame, instruction->value.store.address, "t0") &&
               load_core_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_scalar_store_for_program(file, program, stored_type, "t1", "t0");
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, function, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        return emit_register_output_inline_asm(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return emit_field_address(file, program, frame, instruction);
    }
    return false;
}

static bool emit_block_label(FILE *file, const char *symbol_name, MinicCoreBlockId block_id) {
    return fprintf(file, ".L%s_core_bb%" PRIu32 ":\n", symbol_name, block_id) >= 0;
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
                return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                return_value.slot_count == 0U || return_value.slot_count > 2U ||
                !core_object_offset(program, function, terminator->return_object, &object_offset) ||
                !emit_sp_address(file, "t0", object_offset) ||
                !minic_riscv64_emit_integer_aggregate_load_chunk(
                    file, program, function->return_type, 0U, "a0", "t0") ||
                (return_value.slot_count == 2U &&
                 !minic_riscv64_emit_integer_aggregate_load_chunk(
                     file, program, function->return_type, 1U, "a1", "t0"))) {
                return false;
            }
        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !load_core_value(file, frame, terminator->return_value, "a0")) {
            return false;
        }
        return fprintf(file, "  j .L%s_core_return\n", symbol_name) >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(
                   file, "  j .L%s_core_bb%" PRIu32 "\n", symbol_name, terminator->branch_target) >=
               0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        if (!load_core_value(file, frame, terminator->conditional.condition, "t0") ||
            fprintf(file,
                    "  bnez t0, .L%s_core_bb%" PRIu32 "\n"
                    "  j .L%s_core_bb%" PRIu32 "\n",
                    symbol_name,
                    terminator->conditional.when_true,
                    symbol_name,
                    terminator->conditional.when_false) < 0) {
            return false;
        }
        return true;
    }
    return false;
}

static bool emit_core_function_basic_v0_with_symbol(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicCoreFunction *function,
                                                    const MinicRiscv64FunctionSymbol *symbol) {
    MinicRiscv64CoreFrame frame;
    const char *symbol_name;
    size_t block_index;

    if (file == NULL || symbol == NULL || symbol->symbol_name == NULL ||
        symbol->symbol_name[0] == '\0' || !core_function_can_emit_basic_v0(program, function) ||
        !core_frame_initialize(program, function, &frame)) {
        return false;
    }
    symbol_name = symbol->symbol_name;
    if (!minic_riscv64_emit_function_symbol_begin(file, symbol) ||
        !minic_riscv64_emit_stack_allocate(file, frame.frame_size)) {
        return false;
    }
    if (frame.saves_return_address &&
        !minic_riscv64_emit_sp_store64(file, "ra", frame.return_address_offset)) {
        return false;
    }
    if (fprintf(file, "  j .L%s_core_bb%" PRIu32 "\n", symbol_name, function->entry_block) < 0) {
        return false;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block;
        size_t instruction_index;

        block = &function->blocks[block_index];
        if (!emit_block_label(file, symbol_name, (MinicCoreBlockId)block_index)) {
            return false;
        }
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            MinicCoreInstructionId instruction_id;

            instruction_id = block->instructions[instruction_index];
            if (instruction_id >= function->instruction_count ||
                !emit_instruction(
                    file, program, function, &frame, &function->instructions[instruction_id])) {
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
    if (!minic_riscv64_emit_stack_release(file, frame.frame_size) || fprintf(file, "  ret\n") < 0 ||
        !minic_riscv64_emit_function_symbol_end(file, symbol)) {
        return false;
    }
    return true;
}

bool minic_riscv64_emit_core_function_basic_v0_with_symbol(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64FunctionSymbol *symbol) {
    return emit_core_function_basic_v0_with_symbol(file, NULL, function, symbol);
}

bool minic_riscv64_emit_core_function_basic_v0_for_program_with_symbol(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64FunctionSymbol *symbol) {
    return program != NULL &&
           emit_core_function_basic_v0_with_symbol(file, program, function, symbol);
}
