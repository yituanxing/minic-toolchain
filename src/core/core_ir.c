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
    size_t callee_index;

    if (function == NULL) {
        return;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        free(function->blocks[block_index].instructions);
    }
    for (callee_index = 0U; callee_index < function->callee_count; ++callee_index) {
        free(function->callees[callee_index].name);
        free(function->callees[callee_index].parameter_types);
    }
    free(function->name);
    free(function->parameter_types);
    free(function->callees);
    free(function->call_arguments);
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

static bool core_call_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool callee_signature_equal(const MinicCoreCallee *callee,
                                   const char *name,
                                   size_t name_length,
                                   MinicType return_type,
                                   const MinicType *parameter_types,
                                   size_t parameter_count) {
    size_t index;

    if (callee == NULL || name == NULL || callee->name == NULL ||
        callee->name_length != name_length || memcmp(callee->name, name, name_length) != 0 ||
        !minic_type_equal(callee->return_type, return_type) ||
        callee->parameter_count != parameter_count) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_type_equal(callee->parameter_types[index], parameter_types[index])) {
            return false;
        }
    }
    return true;
}

bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    MinicCoreCalleeId *callee_id) {
    MinicCoreCallee stored;
    size_t index;

    if (function == NULL || name == NULL || name_length == 0U || callee_id == NULL ||
        function->callee_count >= (size_t)UINT32_MAX ||
        (!minic_type_is_void(return_type) && !core_call_scalar_type(return_type)) ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*stored.parameter_types)) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!core_call_scalar_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *existing;

        existing = &function->callees[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            if (!callee_signature_equal(
                    existing, name, name_length, return_type, parameter_types, parameter_count)) {
                return false;
            }
            *callee_id = (MinicCoreCalleeId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.name = copy_name(name, name_length);
    if (stored.name == NULL) {
        return false;
    }
    if (parameter_count != 0U) {
        stored.parameter_types =
            (MinicType *)malloc(parameter_count * sizeof(*stored.parameter_types));
        if (stored.parameter_types == NULL) {
            free(stored.name);
            return false;
        }
        (void)memcpy(stored.parameter_types,
                     parameter_types,
                     parameter_count * sizeof(*stored.parameter_types));
    }
    stored.name_length = name_length;
    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
    if (!grow_array((void **)&function->callees,
                    &function->callee_capacity,
                    function->callee_count,
                    sizeof(*function->callees))) {
        free(stored.name);
        free(stored.parameter_types);
        return false;
    }
    function->callees[function->callee_count] = stored;
    *callee_id = (MinicCoreCalleeId)function->callee_count;
    function->callee_count += 1U;
    return true;
}

bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreValueId *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin) {
    size_t index;
    size_t start;

    if (function == NULL || argument_begin == NULL || (argument_count != 0U && arguments == NULL) ||
        argument_count > SIZE_MAX - function->call_argument_count) {
        return false;
    }
    start = function->call_argument_count;
    for (index = 0U; index < argument_count; ++index) {
        if (!grow_array((void **)&function->call_arguments,
                        &function->call_argument_capacity,
                        function->call_argument_count,
                        sizeof(*function->call_arguments))) {
            function->call_argument_count = start;
            return false;
        }
        function->call_arguments[function->call_argument_count] = arguments[index];
        function->call_argument_count += 1U;
    }
    *argument_begin = start;
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
    case MINIC_CORE_INSTRUCTION_ADDRESS_OFFSET:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type) &&
               instruction->value.address_offset.base < function->value_count &&
               available_values[instruction->value.address_offset.base] &&
               minic_type_is_pointer(function->values[instruction->value.address_offset.base].type);
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
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_begin > function->call_argument_count ||
            instruction->value.call.argument_count >
                function->call_argument_count - instruction->value.call.argument_begin) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if (instruction->value.call.argument_count != callee->parameter_count ||
            !minic_type_equal(instruction->type, callee->return_type)) {
            return false;
        }
        returns_void = minic_type_is_void(callee->return_type);
        if ((returns_void && instruction->result != MINIC_CORE_VALUE_INVALID) ||
            (!returns_void && !instruction_result_is_valid(function, instruction))) {
            return false;
        }
        argument_end =
            instruction->value.call.argument_begin + instruction->value.call.argument_count;
        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            MinicCoreValueId argument;
            size_t parameter_index;

            argument = function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            if (argument >= function->value_count || !available_values[argument] ||
                !minic_type_equal(function->values[argument].type,
                                  callee->parameter_types[parameter_index])) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

static bool terminator_is_valid(const MinicCoreFunction *function,
                                const MinicCoreTerminator *terminator,
                                const bool *available_values) {
    if (function == NULL || terminator == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (minic_type_is_void(function->return_type)) {
            return terminator->return_value == MINIC_CORE_VALUE_INVALID;
        }
        return terminator->return_value < function->value_count &&
               available_values[terminator->return_value] &&
               minic_type_equal(function->values[terminator->return_value].type,
                                function->return_type);
    case MINIC_CORE_TERMINATOR_BRANCH:
        return terminator->branch_target < function->block_count;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return terminator->conditional.condition < function->value_count &&
               available_values[terminator->conditional.condition] &&
               minic_type_is_integer(function->values[terminator->conditional.condition].type) &&
               terminator->conditional.when_true < function->block_count &&
               terminator->conditional.when_false < function->block_count;
    }
    return false;
}

static bool verify_block(const MinicCoreFunction *function,
                         MinicCoreBlockId block_id,
                         bool *instruction_seen,
                         bool *value_seen,
                         bool *available_values) {
    const MinicCoreBlock *block;
    size_t index;

    block = &function->blocks[block_id];
    if (!storage_shape_is_valid(
            block->instructions, block->instruction_count, block->instruction_capacity) ||
        !block->has_terminator) {
        return false;
    }
    if (function->value_count != 0U) {
        (void)memset(available_values, 0, function->value_count * sizeof(*available_values));
    }
    for (index = 0U; index < block->instruction_count; ++index) {
        MinicCoreInstructionId instruction_id;
        const MinicCoreInstruction *instruction;

        instruction_id = block->instructions[index];
        if (instruction_id >= function->instruction_count || instruction_seen[instruction_id]) {
            return false;
        }
        instruction = &function->instructions[instruction_id];
        if (!instruction_is_valid(function, instruction, available_values)) {
            return false;
        }
        if (instruction->result != MINIC_CORE_VALUE_INVALID) {
            const MinicCoreValue *result;

            result = &function->values[instruction->result];
            if (value_seen[instruction->result] || result->definition != instruction_id) {
                return false;
            }
            available_values[instruction->result] = true;
            value_seen[instruction->result] = true;
        }
        instruction_seen[instruction_id] = true;
    }
    return terminator_is_valid(function, &block->terminator, available_values);
}

bool minic_core_function_verify(const MinicCoreFunction *function) {
    bool *available_values;
    bool *instruction_seen;
    bool *value_seen;
    size_t block_index;
    size_t index;
    bool valid;

    if (function == NULL || function->phase != MINIC_CORE_PHASE_EXECUTION_SHADOW ||
        function->name == NULL || function->name_length == 0U ||
        (function->parameter_count != 0U && function->parameter_types == NULL) ||
        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(function->call_arguments,
                                function->call_argument_count,
                                function->call_argument_capacity) ||
        !storage_shape_is_valid(
            function->objects, function->object_count, function->object_capacity) ||
        !storage_shape_is_valid(
            function->values, function->value_count, function->value_capacity) ||
        !storage_shape_is_valid(
            function->instructions, function->instruction_count, function->instruction_capacity) ||
        !storage_shape_is_valid(
            function->blocks, function->block_count, function->block_capacity) ||
        function->block_count == 0U || function->entry_block != 0U ||
        function->value_count > function->instruction_count) {
        return false;
    }
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *callee;
        size_t parameter_index;

        callee = &function->callees[index];
        if (callee->name == NULL || callee->name_length == 0U ||
            (!minic_type_is_void(callee->return_type) &&
             !core_call_scalar_type(callee->return_type)) ||
            (callee->parameter_count != 0U && callee->parameter_types == NULL)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
            if (!core_call_scalar_type(callee->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    instruction_seen = function->instruction_count == 0U
                           ? NULL
                           : (bool *)calloc(function->instruction_count, sizeof(*instruction_seen));
    value_seen = function->value_count == 0U
                     ? NULL
                     : (bool *)calloc(function->value_count, sizeof(*value_seen));
    available_values = function->value_count == 0U
                           ? NULL
                           : (bool *)calloc(function->value_count, sizeof(*available_values));
    if ((function->instruction_count != 0U && instruction_seen == NULL) ||
        (function->value_count != 0U && (value_seen == NULL || available_values == NULL))) {
        free(instruction_seen);
        free(value_seen);
        free(available_values);
        return false;
    }
    valid = true;
    for (block_index = 0U; valid && block_index < function->block_count; ++block_index) {
        valid = verify_block(function,
                             (MinicCoreBlockId)block_index,
                             instruction_seen,
                             value_seen,
                             available_values);
    }
    for (index = 0U; valid && index < function->instruction_count; ++index) {
        valid = instruction_seen[index];
    }
    for (index = 0U; valid && index < function->value_count; ++index) {
        valid = value_seen[index];
    }
    free(instruction_seen);
    free(value_seen);
    free(available_values);
    return valid;
}

static bool dump_instruction(FILE *output,
                             const MinicCoreFunction *function,
                             const MinicCoreInstruction *instruction) {
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return fprintf(output,
                       "  %%%" PRIu32 " = const.int %" PRId64 "\n",
                       instruction->result,
                       instruction->value.integer_value) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        return fprintf(output,
                       "  %%%" PRIu32 " = add.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return fprintf(output,
                       "  %%%" PRIu32 " = parameter %zu\n",
                       instruction->result,
                       instruction->value.parameter_index) >= 0;
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = object.addr %%o%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.object_id) >= 0;
    case MINIC_CORE_INSTRUCTION_ADDRESS_OFFSET:
        return fprintf(output,
                       "  %%%" PRIu32 " = addr.offset %%%" PRIu32 ", %" PRId64 "\n",
                       instruction->result,
                       instruction->value.address_offset.base,
                       instruction->value.address_offset.byte_offset) >= 0;
    case MINIC_CORE_INSTRUCTION_LOAD:
        return fprintf(output,
                       "  %%%" PRIu32 " = load%s %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.load.is_volatile ? ".volatile" : "",
                       instruction->value.load.address) >= 0;
    case MINIC_CORE_INSTRUCTION_STORE:
        return fprintf(output,
                       "  store%s %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->value.store.is_volatile ? ".volatile" : "",
                       instruction->value.store.stored_value,
                       instruction->value.store.address) >= 0;
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;

        if (function == NULL || instruction->value.call.callee_id >= function->callee_count) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output, "  call @") < 0) {
                return false;
            }
        } else if (fprintf(output, "  %%%" PRIu32 " = call @", instruction->result) < 0) {
            return false;
        }
        if (fwrite(callee->name, 1U, callee->name_length, output) != callee->name_length ||
            fprintf(output, "(") < 0) {
            return false;
        }
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            MinicCoreValueId argument;

            argument =
                function->call_arguments[instruction->value.call.argument_begin + argument_index];
            if ((argument_index != 0U && fprintf(output, ", ") < 0) ||
                fprintf(output, "%%%" PRIu32, argument) < 0) {
                return false;
            }
        }
        return fprintf(output, ")\n") >= 0;
    }
    }
    return false;
}

static bool dump_terminator(FILE *output, const MinicCoreTerminator *terminator) {
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (terminator->return_value == MINIC_CORE_VALUE_INVALID) {
            return fprintf(output, "  return\n") >= 0;
        }
        return fprintf(output, "  return %%%" PRIu32 "\n", terminator->return_value) >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(output, "  br bb%" PRIu32 "\n", terminator->branch_target) >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return fprintf(output,
                       "  cond_br %%%" PRIu32 ", bb%" PRIu32 ", bb%" PRIu32 "\n",
                       terminator->conditional.condition,
                       terminator->conditional.when_true,
                       terminator->conditional.when_false) >= 0;
    }
    return false;
}

bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {
    size_t block_index;
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
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block;

        block = &function->blocks[block_index];
        if (fprintf(output, "bb%zu:\n", block_index) < 0) {
            return false;
        }
        for (index = 0U; index < block->instruction_count; ++index) {
            const MinicCoreInstruction *instruction;

            instruction = &function->instructions[block->instructions[index]];
            if (!dump_instruction(output, function, instruction)) {
                return false;
            }
        }
        if (!dump_terminator(output, &block->terminator)) {
            return false;
        }
    }
    return true;
}
