#include "target/riscv64/core_codegen.h"

#include "target/riscv64/codegen_internal.h"
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

static bool core_frame_initialize(const MinicCoreFunction *function, MinicRiscv64CoreFrame *frame) {
    size_t slot_count;
    size_t storage_size;

    if (function == NULL || frame == NULL ||
        function->object_count > SIZE_MAX - function->value_count) {
        return false;
    }
    slot_count = function->object_count + function->value_count;
    frame->saves_return_address = core_function_has_call(function);
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (slot_count > SIZE_MAX / 8U) {
            return false;
        }
        frame->return_address_offset = slot_count * 8U;
        if (slot_count == SIZE_MAX) {
            return false;
        }
        slot_count += 1U;
    }
    if (slot_count > SIZE_MAX / 8U) {
        return false;
    }
    storage_size = slot_count * 8U;
    if (!align_up(storage_size, 16U, &frame->frame_size)) {
        return false;
    }
    frame->object_count = function->object_count;
    frame->value_count = function->value_count;
    return true;
}

static bool core_object_offset(const MinicRiscv64CoreFrame *frame,
                               MinicCoreObjectId object_id,
                               size_t *offset) {
    if (frame == NULL || offset == NULL || object_id >= frame->object_count) {
        return false;
    }
    *offset = (size_t)object_id * 8U;
    return true;
}

static bool
core_value_offset(const MinicRiscv64CoreFrame *frame, MinicCoreValueId value_id, size_t *offset) {
    size_t slot_index;

    if (frame == NULL || offset == NULL || value_id >= frame->value_count ||
        frame->object_count > SIZE_MAX - (size_t)value_id) {
        return false;
    }
    slot_index = frame->object_count + (size_t)value_id;
    if (slot_index > SIZE_MAX / 8U) {
        return false;
    }
    *offset = slot_index * 8U;
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
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
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
    }
    return false;
}

static bool core_function_can_emit_basic_v0(const MinicC0Program *program,
                                            const MinicCoreFunction *function) {
    size_t index;

    if (function == NULL || !minic_core_function_verify(function) ||
        (!minic_type_is_void(function->return_type) && !core_scalar_type(function->return_type))) {
        return false;
    }
    for (index = 0U; index < function->parameter_count; ++index) {
        if (!core_scalar_type(function->parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->object_count; ++index) {
        if (!core_scalar_type(function->objects[index].type)) {
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

static bool emit_parameter(FILE *file,
                           const MinicCoreFunction *function,
                           const MinicRiscv64CoreFrame *frame,
                           const MinicCoreInstruction *instruction) {
    size_t parameter_index;
    size_t incoming_offset;

    parameter_index = instruction->value.parameter_index;
    if (parameter_index >= function->parameter_count) {
        return false;
    }
    if (parameter_index < 8U) {
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
        !minic_riscv64_emit_integer_conversion(file, instruction->type, "t0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "t0");
}

static bool emit_call(FILE *file,
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
        !minic_riscv64_emit_integer_conversion(file, instruction->type, "a0")) {
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
            !minic_riscv64_emit_integer_conversion(file, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  add t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion(file, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            !minic_riscv64_emit_integer_conversion(file, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  neg t0, t0\n") < 0 ||
            !minic_riscv64_emit_integer_conversion(file, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  seqz t0, t0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!core_object_offset(frame, instruction->value.object_id, &object_offset) ||
            !emit_sp_address(file, "t0", object_offset)) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_scalar_load(file, instruction->type, "t1", "t0")) {
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
               minic_riscv64_emit_scalar_store(file, stored_type, "t1", "t0");
    }
    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return emit_field_address(file, program, frame, instruction);
    }
    return false;
}

static bool emit_block_label(FILE *file, const char *symbol_name, MinicCoreBlockId block_id) {
    return fprintf(file, ".L%s_core_bb%" PRIu32 ":\n", symbol_name, block_id) >= 0;
}

static bool emit_terminator(FILE *file,
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
        if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
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
        !core_frame_initialize(function, &frame)) {
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
            !emit_terminator(file, function, &frame, symbol_name, &block->terminator)) {
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
