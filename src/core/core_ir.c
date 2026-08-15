#include "core/core_ir.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool grow_array(void **data, size_t *capacity, size_t count, size_t element_size) {
    void *resized;
    size_t new_capacity;

    if (data == NULL || capacity == NULL || element_size == 0U || count > *capacity) {
        return false;
    }
    if (count < *capacity) {
        return true;
    }
    new_capacity = *capacity == 0U ? 8U : *capacity * 2U;
    if (new_capacity <= count || new_capacity < *capacity ||
        new_capacity > SIZE_MAX / element_size) {
        return false;
    }
    resized = realloc(*data, new_capacity * element_size);
    if (resized == NULL) {
        return false;
    }
    *data = resized;
    *capacity = new_capacity;
    return true;
}

static char *copy_name(const char *name, size_t name_length) {
    char *copy;

    if (name == NULL || name_length == 0U || name_length == SIZE_MAX) {
        return NULL;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    return copy;
}

void minic_core_function_initialize(MinicCoreFunction *function) {
    if (function == NULL) {
        return;
    }
    (void)memset(function, 0, sizeof(*function));
    function->phase = MINIC_CORE_PHASE_EXECUTION_SHADOW;
    function->entry_block = MINIC_CORE_BLOCK_INVALID;
}

void minic_core_function_destroy(MinicCoreFunction *function) {
    size_t block_index;

    if (function == NULL) {
        return;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        free(function->blocks[block_index].instructions);
    }
    free(function->name);
    free(function->parameter_types);
    free(function->objects);
    free(function->values);
    free(function->instructions);
    free(function->blocks);
    minic_core_function_initialize(function);
}

bool minic_core_function_set_signature(MinicCoreFunction *function,
                                       const char *name,
                                       size_t name_length,
                                       MinicType return_type,
                                       const MinicType *parameter_types,
                                       size_t parameter_count) {
    char *name_copy;
    MinicType *parameters_copy;

    if (function == NULL || function->name != NULL || function->parameter_types != NULL ||
        function->parameter_count != 0U || name == NULL || name_length == 0U ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*parameters_copy)) {
        return false;
    }
    name_copy = copy_name(name, name_length);
    if (name_copy == NULL) {
        return false;
    }
    parameters_copy = NULL;
    if (parameter_count != 0U) {
        parameters_copy = (MinicType *)malloc(parameter_count * sizeof(*parameters_copy));
        if (parameters_copy == NULL) {
            free(name_copy);
            return false;
        }
        (void)memcpy(parameters_copy, parameter_types, parameter_count * sizeof(*parameters_copy));
    }
    function->name = name_copy;
    function->name_length = name_length;
    function->return_type = return_type;
    function->parameter_types = parameters_copy;
    function->parameter_count = parameter_count;
    return true;
}

bool minic_core_function_add_block(MinicCoreFunction *function, MinicCoreBlockId *block_id) {
    MinicCoreBlockId new_id;

    if (function == NULL || block_id == NULL || function->block_count >= (size_t)UINT32_MAX ||
        !grow_array((void **)&function->blocks,
                    &function->block_capacity,
                    function->block_count,
                    sizeof(*function->blocks))) {
        return false;
    }
    new_id = (MinicCoreBlockId)function->block_count;
    (void)memset(&function->blocks[function->block_count], 0, sizeof(*function->blocks));
    function->block_count += 1U;
    if (function->entry_block == MINIC_CORE_BLOCK_INVALID) {
        function->entry_block = new_id;
    }
    *block_id = new_id;
    return true;
}

bool minic_core_function_add_object(MinicCoreFunction *function,
                                    MinicSourceSpan span,
                                    MinicType type,
                                    MinicCoreObjectId *object_id) {
    MinicCoreObjectId new_id;

    if (function == NULL || object_id == NULL || function->object_count >= (size_t)UINT32_MAX ||
        minic_type_is_void(type) || minic_type_is_function(type) ||
        !grow_array((void **)&function->objects,
                    &function->object_capacity,
                    function->object_count,
                    sizeof(*function->objects))) {
        return false;
    }
    new_id = (MinicCoreObjectId)function->object_count;
    function->objects[function->object_count].span = span;
    function->objects[function->object_count].type = type;
    function->object_count += 1U;
    *object_id = new_id;
    return true;
}

static bool reserve_instruction(MinicCoreFunction *function, MinicCoreBlock *block) {
    return grow_array((void **)&function->instructions,
                      &function->instruction_capacity,
                      function->instruction_count,
                      sizeof(*function->instructions)) &&
           grow_array((void **)&block->instructions,
                      &block->instruction_capacity,
                      block->instruction_count,
                      sizeof(*block->instructions));
}

static void append_reserved_instruction(MinicCoreFunction *function,
                                        MinicCoreBlock *block,
                                        const MinicCoreInstruction *instruction,
                                        MinicCoreInstructionId instruction_id) {
    function->instructions[function->instruction_count] = *instruction;
    function->instruction_count += 1U;
    block->instructions[block->instruction_count] = instruction_id;
    block->instruction_count += 1U;
}

bool minic_core_function_append_value_instruction(MinicCoreFunction *function,
                                                  MinicCoreBlockId block_id,
                                                  const MinicCoreInstruction *instruction,
                                                  MinicCoreValueId *value_id) {
    MinicCoreBlock *block;
    MinicCoreInstruction stored;
    MinicCoreInstructionId instruction_id;
    MinicCoreValueId result_id;

    if (function == NULL || instruction == NULL || value_id == NULL ||
        block_id >= function->block_count || function->instruction_count >= (size_t)UINT32_MAX ||
        function->value_count >= (size_t)UINT32_MAX) {
        return false;
    }
    block = &function->blocks[block_id];
    if (block->has_terminator || !reserve_instruction(function, block) ||
        !grow_array((void **)&function->values,
                    &function->value_capacity,
                    function->value_count,
                    sizeof(*function->values))) {
        return false;
    }
    instruction_id = (MinicCoreInstructionId)function->instruction_count;
    result_id = (MinicCoreValueId)function->value_count;
    stored = *instruction;
    stored.result = result_id;
    function->values[function->value_count].type = stored.type;
    function->values[function->value_count].definition = instruction_id;
    function->value_count += 1U;
    append_reserved_instruction(function, block, &stored, instruction_id);
    *value_id = result_id;
    return true;
}

bool minic_core_function_append_effect_instruction(MinicCoreFunction *function,
                                                   MinicCoreBlockId block_id,
                                                   const MinicCoreInstruction *instruction) {
    MinicCoreBlock *block;
    MinicCoreInstruction stored;
    MinicCoreInstructionId instruction_id;

    if (function == NULL || instruction == NULL || block_id >= function->block_count ||
        function->instruction_count >= (size_t)UINT32_MAX) {
        return false;
    }
    block = &function->blocks[block_id];
    if (block->has_terminator || !reserve_instruction(function, block)) {
        return false;
    }
    instruction_id = (MinicCoreInstructionId)function->instruction_count;
    stored = *instruction;
    stored.result = MINIC_CORE_VALUE_INVALID;
    append_reserved_instruction(function, block, &stored, instruction_id);
    return true;
}

bool minic_core_function_set_terminator(MinicCoreFunction *function,
                                        MinicCoreBlockId block_id,
                                        const MinicCoreTerminator *terminator) {
    MinicCoreBlock *block;

    if (function == NULL || terminator == NULL || block_id >= function->block_count) {
        return false;
    }
    block = &function->blocks[block_id];
    if (block->has_terminator) {
        return false;
    }
    block->terminator = *terminator;
    block->has_terminator = true;
    return true;
}

static bool storage_shape_is_valid(const void *data, size_t count, size_t capacity) {
    return count <= capacity && (count == 0U || data != NULL);
}

static bool instruction_result_is_valid(const MinicCoreFunction *function,
                                        const MinicCoreInstruction *instruction) {
    return instruction->result < function->value_count &&
           minic_type_equal(function->values[instruction->result].type, instruction->type);
}

static bool available_pointer_pointee(const MinicCoreFunction *function,
                                      const bool *available_values,
                                      MinicCoreValueId address,
                                      MinicType *pointee) {
    if (address >= function->value_count || !available_values[address]) {
        return false;
    }
    return minic_type_pointee(function->values[address].type, pointee);
}

static bool instruction_is_valid(const MinicCoreFunction *function,
                                 const MinicCoreInstruction *instruction,
                                 const bool *available_values) {
    const MinicCoreValue *left;
    const MinicCoreValue *right;

    if (function == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_is_integer(instruction->type) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_equal(left->type, instruction->type) &&
               minic_type_equal(right->type, instruction->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.parameter_index < function->parameter_count &&
               minic_type_equal(function->parameter_types[instruction->value.parameter_index],
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
        MinicType pointer_type;

        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.object_id >= function->object_count ||
            !minic_type_pointer_to(function->objects[instruction->value.object_id].type,
                                   &pointer_type)) {
            return false;
        }
        return minic_type_equal(pointer_type, instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_LOAD: {
        MinicType pointee;
        MinicType value_type;

        if (!instruction_result_is_valid(function, instruction) ||
            !available_pointer_pointee(
                function, available_values, instruction->value.load.address, &pointee) ||
            !minic_type_unqualified(pointee, &value_type)) {
            return false;
        }
        return minic_type_equal(value_type, instruction->type) &&
               instruction->value.load.is_volatile == minic_type_is_volatile(pointee);
    }
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicType pointee;
        MinicType value_type;
        MinicCoreValueId stored_value;

        stored_value = instruction->value.store.stored_value;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) || stored_value >= function->value_count ||
            !available_values[stored_value] ||
            !available_pointer_pointee(
                function, available_values, instruction->value.store.address, &pointee) ||
            !minic_type_unqualified(pointee, &value_type)) {
            return false;
        }
        return minic_type_equal(value_type, function->values[stored_value].type) &&
               instruction->value.store.is_volatile == minic_type_is_volatile(pointee);
    }
    }
    return false;
}

bool minic_core_function_verify(const MinicCoreFunction *function) {
    const MinicCoreBlock *block;
    bool *instruction_seen;
    bool *available_values;
    size_t index;
    bool valid;

    if (function == NULL || function->phase != MINIC_CORE_PHASE_EXECUTION_SHADOW ||
        function->name == NULL || function->name_length == 0U ||
        (function->parameter_count != 0U && function->parameter_types == NULL) ||
        !storage_shape_is_valid(
            function->objects, function->object_count, function->object_capacity) ||
        !storage_shape_is_valid(
            function->values, function->value_count, function->value_capacity) ||
        !storage_shape_is_valid(
            function->instructions, function->instruction_count, function->instruction_capacity) ||
        !storage_shape_is_valid(
            function->blocks, function->block_count, function->block_capacity) ||
        function->block_count != 1U || function->entry_block != 0U ||
        function->value_count > function->instruction_count) {
        return false;
    }
    block = &function->blocks[0];
    if (!storage_shape_is_valid(
            block->instructions, block->instruction_count, block->instruction_capacity) ||
        block->instruction_count != function->instruction_count || !block->has_terminator) {
        return false;
    }
    instruction_seen = function->instruction_count == 0U
                           ? NULL
                           : (bool *)calloc(function->instruction_count, sizeof(*instruction_seen));
    available_values = function->value_count == 0U
                           ? NULL
                           : (bool *)calloc(function->value_count, sizeof(*available_values));
    if ((function->instruction_count != 0U && instruction_seen == NULL) ||
        (function->value_count != 0U && available_values == NULL)) {
        free(instruction_seen);
        free(available_values);
        return false;
    }
    valid = true;
    for (index = 0U; valid && index < block->instruction_count; ++index) {
        MinicCoreInstructionId instruction_id;
        const MinicCoreInstruction *instruction;

        instruction_id = block->instructions[index];
        if (instruction_id >= function->instruction_count || instruction_seen[instruction_id]) {
            valid = false;
            break;
        }
        instruction = &function->instructions[instruction_id];
        if (!instruction_is_valid(function, instruction, available_values)) {
            valid = false;
            break;
        }
        if (instruction->result != MINIC_CORE_VALUE_INVALID) {
            const MinicCoreValue *result;

            result = &function->values[instruction->result];
            if (available_values[instruction->result] || result->definition != instruction_id) {
                valid = false;
                break;
            }
            available_values[instruction->result] = true;
        }
        instruction_seen[instruction_id] = true;
    }
    for (index = 0U; valid && index < function->instruction_count; ++index) {
        valid = instruction_seen[index];
    }
    if (valid) {
        if (block->terminator.kind != MINIC_CORE_TERMINATOR_RETURN) {
            valid = false;
        } else if (minic_type_is_void(function->return_type)) {
            valid = block->terminator.return_value == MINIC_CORE_VALUE_INVALID;
        } else {
            MinicCoreValueId return_value;

            return_value = block->terminator.return_value;
            valid = return_value < function->value_count && available_values[return_value] &&
                    minic_type_equal(function->values[return_value].type, function->return_type);
        }
    }
    free(instruction_seen);
    free(available_values);
    return valid;
}

bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {
    const MinicCoreBlock *block;
    size_t index;

    if (output == NULL || !minic_core_function_verify(function) ||
        fprintf(output, "core function @") < 0 ||
        fwrite(function->name, 1U, function->name_length, output) != function->name_length ||
        fprintf(output, "\n") < 0) {
        return false;
    }
    for (index = 0U; index < function->object_count; ++index) {
        if (fprintf(output,
                    "object %%o%" PRIu32 "%s\n",
                    (MinicCoreObjectId)index,
                    minic_type_is_volatile(function->objects[index].type) ? " volatile" : "") < 0) {
            return false;
        }
    }
    if (fprintf(output, "bb0:\n") < 0) {
        return false;
    }
    block = &function->blocks[0];
    for (index = 0U; index < block->instruction_count; ++index) {
        const MinicCoreInstruction *instruction;

        instruction = &function->instructions[block->instructions[index]];
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
            if (fprintf(output,
                        "  %%%" PRIu32 " = const.int %" PRId64 "\n",
                        instruction->result,
                        instruction->value.integer_value) < 0) {
                return false;
            }
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
            if (fprintf(output,
                        "  %%%" PRIu32 " = add.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                        instruction->result,
                        instruction->value.binary.left,
                        instruction->value.binary.right) < 0) {
                return false;
            }
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
            if (fprintf(output,
                        "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                        instruction->result,
                        instruction->value.operand) < 0) {
                return false;
            }
            break;
        case MINIC_CORE_INSTRUCTION_PARAMETER:
            if (fprintf(output,
                        "  %%%" PRIu32 " = parameter %zu\n",
                        instruction->result,
                        instruction->value.parameter_index) < 0) {
                return false;
            }
            break;
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
            if (fprintf(output,
                        "  %%%" PRIu32 " = object.addr %%o%" PRIu32 "\n",
                        instruction->result,
                        instruction->value.object_id) < 0) {
                return false;
            }
            break;
        case MINIC_CORE_INSTRUCTION_LOAD:
            if (fprintf(output,
                        "  %%%" PRIu32 " = load%s %%%" PRIu32 "\n",
                        instruction->result,
                        instruction->value.load.is_volatile ? ".volatile" : "",
                        instruction->value.load.address) < 0) {
                return false;
            }
            break;
        case MINIC_CORE_INSTRUCTION_STORE:
            if (fprintf(output,
                        "  store%s %%%" PRIu32 ", %%%" PRIu32 "\n",
                        instruction->value.store.is_volatile ? ".volatile" : "",
                        instruction->value.store.stored_value,
                        instruction->value.store.address) < 0) {
                return false;
            }
            break;
        }
    }
    if (block->terminator.return_value == MINIC_CORE_VALUE_INVALID) {
        return fprintf(output, "  return\n") >= 0;
    }
    return fprintf(output, "  return %%%" PRIu32 "\n", block->terminator.return_value) >= 0;
}
