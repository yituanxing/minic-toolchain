#include "core/core_ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core IR verification owns instruction admission, block/value invariants
 * and the whole-function fail-closed acceptance contract. */
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

bool minic_core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type) {
    return (minic_type_is_pointer(target_type) &&
            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||
           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));
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
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return instruction_result_is_valid(function, instruction) &&
               (minic_type_is_float(instruction->type) ||
                minic_type_is_double(instruction->type) ||
                minic_type_is_long_double(instruction->type));
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_is_float(instruction->type) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_float(left->type) &&
               minic_type_equal(left->type, instruction->type) &&
               minic_type_equal(right->type, instruction->type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_is_double(instruction->type) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_double(left->type) &&
               minic_type_equal(left->type, instruction->type) &&
               minic_type_equal(right->type, instruction->type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_double(left->type) &&
               minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
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
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
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
               minic_type_is_integer(right->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_integer(left->type) && minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_pointer(left->type) && minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return (minic_type_is_integer(left->type) || minic_type_is_pointer(left->type)) &&
               minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;

        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_bool()) ||
            (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
            instruction->value.integer_overflow.left >= function->value_count ||
            instruction->value.integer_overflow.right >= function->value_count ||
            instruction->value.integer_overflow.result_address >= function->value_count ||
            !available_values[instruction->value.integer_overflow.left] ||
            !available_values[instruction->value.integer_overflow.right] ||
            !available_values[instruction->value.integer_overflow.result_address] ||
            !minic_type_pointee(
                function->values[instruction->value.integer_overflow.result_address].type,
                &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {
            return false;
        }
        left = &function->values[instruction->value.integer_overflow.left];
        right = &function->values[instruction->value.integer_overflow.right];
        return minic_type_is_integer(left->type) && minic_type_is_integer(right->type);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_double(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_double(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_float(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_float(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_double(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_double(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_core_scalar_bitcast_types_valid(
                   instruction->type, function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_double(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_equal(function->values[instruction->value.operand].type,
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_equal(function->values[instruction->value.operand].type,
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_equal(instruction->type, minic_type_int()) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_unsigned_integer(
                   function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_equal(instruction->type, minic_type_int()) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               (minic_type_is_integer(function->values[instruction->value.operand].type) ||
                minic_type_is_pointer(function->values[instruction->value.operand].type));
    /* M79_CALL_FRAME_RETURN_ADDRESS: Core validates the semantic shape only.
       Backend support for a particular kind/level pair is a target concern. */
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {
        MinicType pointee;

        return instruction_result_is_valid(function, instruction) &&
               (instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
                instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
               instruction->value.call_frame_address.level <= 1U &&
               (instruction->value.call_frame_address.level == 0U ||
                instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
               minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);
    }
    /* M123_VARIADIC_ARGUMENT_ADDRESS: Core validates only that the semantic
       cursor is represented as a pointer value. Whether a target ABI can
       materialize it is a backend capability question. */
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type);
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY: {
        const MinicCoreValueId dst = instruction->value.variadic_copy.destination;
        const MinicCoreValueId src = instruction->value.variadic_copy.source;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type) &&
               dst < function->value_count && src < function->value_count &&
               available_values[dst] && available_values[src] &&
               minic_type_is_pointer(function->values[dst].type) &&
               minic_type_equal(function->values[dst].type,
                                function->values[src].type);
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR: {
        const MinicCoreValueId list = instruction->value.operand;
        return instruction_result_is_valid(function, instruction) &&
               (minic_type_is_integer(instruction->type) ||
                minic_type_is_pointer(instruction->type) ||
                minic_type_is_double(instruction->type)) &&
               list < function->value_count && available_values[list] &&
               minic_type_is_pointer(function->values[list].type);
    }
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
        MinicCoreValueId size_value = instruction->value.operand;
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type) &&
               size_value < function->value_count && available_values[size_value] &&
               minic_type_equal(function->values[size_value].type, minic_type_unsigned_long());
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.parameter_index < function->parameter_count &&
               minic_type_equal(function->parameter_types[instruction->value.parameter_index],
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        size_t binding_id = instruction->value.fixed_register_binding_id;

        return instruction_result_is_valid(function, instruction) &&
               binding_id < function->fixed_register_binding_count &&
               function->fixed_register_bindings[binding_id].register_name != NULL &&
               function->fixed_register_bindings[binding_id].register_name_length != 0U &&
               minic_type_equal(function->fixed_register_bindings[binding_id].type,
                                instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE: {
        const size_t id = instruction->value.fixed_register_write.binding_id;
        const MinicCoreValueId source = instruction->value.fixed_register_write.source;
        const MinicCoreFixedRegisterBinding *binding;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            id >= function->fixed_register_binding_count ||
            source >= function->value_count || !available_values[source])
            return false;
        binding = &function->fixed_register_bindings[id];
        return !binding->is_local && binding->register_name != NULL &&
               binding->register_name_length != 0U &&
               minic_type_equal(binding->type, function->values[source].type) &&
               (minic_type_is_integer(binding->type) ||
                minic_type_is_pointer(binding->type));
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {
        MinicCoreObjectId object_id;
        MinicType object_value_type;
        size_t parameter_index;

        object_id = instruction->value.parameter_object.object_id;
        parameter_index = instruction->value.parameter_object.parameter_index;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type) &&
               parameter_index < function->parameter_count && object_id < function->object_count &&
               minic_type_is_record(function->parameter_types[parameter_index]) &&
               minic_type_unqualified(function->objects[object_id].type, &object_value_type) &&
               minic_type_equal(function->parameter_types[parameter_index], object_value_type);
    }
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
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: block addresses are first-class pointer values. */
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type) &&
               instruction->value.block_id < function->block_count;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
        MinicType pointer_type;

        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.global_id >= function->global_count ||
            !minic_type_pointer_to(function->globals[instruction->value.global_id].type,
                                   &pointer_type)) {
            return false;
        }
        return minic_type_equal(pointer_type, instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
        MinicType function_type;
        MinicCoreFunctionSymbolId symbol_id;

        symbol_id = instruction->value.function_symbol_id;
        return instruction_result_is_valid(function, instruction) &&
               symbol_id < function->function_symbol_count &&
               function->function_symbols[symbol_id].name != NULL &&
               function->function_symbols[symbol_id].name_length != 0U &&
               minic_type_pointee(instruction->type, &function_type) &&
               minic_type_is_function(function_type);
    }
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
        MinicCoreValueId base;
        MinicType base_pointee;
        MinicType field_type;

        base = instruction->value.field_address.base;
        if (!instruction_result_is_valid(function, instruction) || base >= function->value_count ||
            !available_values[base] ||
            !minic_type_pointee(function->values[base].type, &base_pointee) ||
            !minic_type_is_record(base_pointee) ||
            base_pointee.record_id != instruction->value.field_address.record_id ||
            instruction->value.field_address.record_id == MINIC_RECORD_INVALID ||
            !minic_type_pointee(instruction->type, &field_type) || minic_type_is_void(field_type) ||
            minic_type_is_function(field_type)) {
            return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        MinicCoreValueId base;
        MinicCoreValueId index;

        base = instruction->value.pointer_offset.base;
        index = instruction->value.pointer_offset.index;
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type) && base < function->value_count &&
               index < function->value_count && available_values[base] && available_values[index] &&
               minic_type_equal(function->values[base].type, instruction->type) &&
               /* M131_ZERO_STRIDE_POINTER_OFFSET: zero is a valid GNU object
                  stride. Keeping POINTER_OFFSET rather than folding it in the
                  producer preserves index evaluation and gives all pointer
                  arithmetic producers one Core semantic owner. */
               minic_type_is_integer(function->values[index].type);
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
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
        MinicType pointee, value_type;
        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.atomic_load.memory_order != 0U ||
            !available_pointer_pointee(
                function, available_values, instruction->value.atomic_load.address, &pointee) ||
            !minic_type_unqualified(pointee, &value_type) ||
            !(minic_type_is_integer(value_type) || minic_type_is_pointer(value_type))) return false;
        return minic_type_equal(value_type, instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE: {
        MinicType pointee, stored_type;
        MinicCoreValueId value = instruction->value.atomic_store.stored_value;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.atomic_store.memory_order != 0U ||
            value >= function->value_count || !available_values[value] ||
            !available_pointer_pointee(
                function, available_values, instruction->value.atomic_store.address, &pointee) ||
            !minic_type_unqualified(pointee, &stored_type) ||
            !(minic_type_is_integer(stored_type) || minic_type_is_pointer(stored_type))) return false;
        return minic_type_equal(stored_type, function->values[value].type);
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
    /* BATCH_M_RECORD_LOAD: source qualification is semantic metadata.  The
       destination is an unqualified private snapshot object. */
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        MinicCoreObjectId destination_object;
        MinicType record_type;
        MinicType source_pointee;
        MinicType source_type;

        destination_object = instruction->value.record_load.destination_object;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_record(instruction->type) &&
               minic_type_unqualified(instruction->type, &record_type) &&
               minic_type_equal(record_type, instruction->type) &&
               destination_object < function->object_count &&
               minic_type_equal(function->objects[destination_object].type,
                                instruction->type) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_load.source_address,
                                         &source_pointee) &&
               minic_type_unqualified(source_pointee, &source_type) &&
               minic_type_equal(source_type, instruction->type) &&
               instruction->value.record_load.is_volatile ==
                   minic_type_is_volatile(source_pointee);
    }
    /* M80_ADDRESS_BACKED_RECORD_COPY: both SSA operands are addresses to the
       same unqualified record type; legality of writing a const-qualified
       destination is already established by the frontend initializer/copy node. */
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        MinicType destination_pointee;
        MinicType destination_type;
        MinicType source_pointee;
        MinicType source_type;
        MinicType record_type;

        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_record(instruction->type) &&
               minic_type_unqualified(instruction->type, &record_type) &&
               minic_type_equal(record_type, instruction->type) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_copy.destination_address,
                                         &destination_pointee) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_copy.source_address,
                                         &source_pointee) &&
               minic_type_unqualified(destination_pointee, &destination_type) &&
               minic_type_unqualified(source_pointee, &source_type) &&
               minic_type_equal(destination_type, instruction->type) &&
               minic_type_equal(source_type, instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        if (!inline_asm->template_text || !inline_asm->is_volatile)
            return false;
        /* Link-time symbolic immediates in qualified asm-goto are owned by
         * Core, regardless of whether the machine template has one or two
         * C labels. Every recorded slot must be a real, nonempty string;
         * partial materialization is not a valid target contract. */
        if (inline_asm->resolved_immediate_count != 0U) {
            size_t i;
            if (!inline_asm->is_goto ||
                inline_asm->goto_target >= function->block_count ||
                inline_asm->resolved_immediate_count > 3U)
                return false;
            for (i = 0U; i < inline_asm->resolved_immediate_count; ++i)
                if (!inline_asm->resolved_immediates[i] ||
                    inline_asm->resolved_immediates[i][0] == '\0')
                    return false;
        }
        if (inline_asm->goto_second_target != MINIC_CORE_BLOCK_INVALID) {
            if (!inline_asm->is_goto ||
                inline_asm->goto_target >= function->block_count ||
                inline_asm->goto_second_target >= function->block_count ||
                inline_asm->goto_target == inline_asm->goto_second_target ||
                inline_asm->resolved_immediate_count != 3U)
                return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (!instruction_result_is_valid(function, instruction) ||
            (!minic_type_is_integer(instruction->type) &&
             !minic_type_is_pointer(instruction->type)) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId operand;

        operand = instruction->value.register_output_input_inline_asm.operand;
        if (!instruction_result_is_valid(function, instruction) ||
            (!minic_type_is_integer(instruction->type) &&
             !minic_type_is_pointer(instruction->type)) ||
            operand >= function->value_count || !available_values[operand] ||
            (!minic_type_is_integer(function->values[operand].type) &&
             !minic_type_is_pointer(function->values[operand].type)) ||
            instruction->value.register_output_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.register_output_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId memory_address;
        MinicCoreValueId operand;
        MinicType pointee;
        size_t memory_index;
        size_t register_index;
        size_t scalar_index;
        bool has_register_output;

        memory_address = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address;
        operand = instruction->value.memory_readwrite_scalar_input_inline_asm.operand;
        memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
        register_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
        scalar_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
        has_register_output = register_index != SIZE_MAX;
        if (memory_address >= function->value_count || operand >= function->value_count ||
            !available_values[memory_address] || !available_values[operand] ||
            !minic_type_pointee(function->values[memory_address].type, &pointee) ||
            (!minic_type_is_integer(pointee) && !minic_type_is_pointer(pointee)) ||
            (!minic_type_is_integer(function->values[operand].type) &&
             !minic_type_is_pointer(function->values[operand].type)) ||
            memory_index > 9U || scalar_index > 9U || memory_index == scalar_index ||
            (has_register_output &&
             (register_index > 9U || register_index == memory_index || register_index == scalar_index)) ||
            instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        if (has_register_output) {
            if (!instruction_result_is_valid(function, instruction) ||
                (!minic_type_is_integer(instruction->type) &&
                 !minic_type_is_pointer(instruction->type))) {
                return false;
            }
        } else if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                   !minic_type_is_void(instruction->type)) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile && inline_asm->has_memory_clobber;
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId operand;

        operand = instruction->value.scalar_input_inline_asm.operand;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) || operand >= function->value_count ||
            !available_values[operand] ||
            (!minic_type_is_integer(function->values[operand].type) &&
             !minic_type_is_pointer(function->values[operand].type)) ||
            instruction->value.scalar_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.scalar_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    /* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: Core records operand roles and
       semantic values/addresses; target register assignment stays in the backend. */
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        bool used_indices[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT] = {false};
        bool has_memory_readwrite = false;
        size_t operand_index;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
            instruction->value.structured_inline_asm.operand_count >
                MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.structured_inline_asm.inline_asm_id];
        if (inline_asm->template_text == NULL ||
            inline_asm->template_length == 0U || !inline_asm->is_volatile) {
            return false;
        }
        /* The C label is a real CFG destination even when the asm also
         * produces typed GNU register outputs. The same Core block IDs
         * are used by opaque and structured goto machine emitters. */
        if (inline_asm->is_goto &&
            (inline_asm->goto_target >= function->block_count ||
             inline_asm->goto_second_target != MINIC_CORE_BLOCK_INVALID))
            return false;
        /* Core only validates the ASM operand/effect shape. A target may
         * qualify a zero-operand instruction which clobbers exactly one
         * machine condition-code resource (x86 "cc"); its ISA emitter
         * revalidates that clobber and literal instruction. Other targets
         * retain their stricter target-only contracts. */
        if (instruction->value.structured_inline_asm.operand_count == 0U)
            return inline_asm->register_clobber_count <= 1U;
        for (operand_index = 0U;
             operand_index < instruction->value.structured_inline_asm.operand_count;
             ++operand_index) {
            const MinicCoreStructuredInlineAsmOperand *binding;
            MinicType pointee;
            MinicType value_type;

            binding = &instruction->value.structured_inline_asm.operands[operand_index];
            if (binding->operand_index >= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
                used_indices[binding->operand_index] ||
                binding->value >= function->value_count || !available_values[binding->value] ||
                (binding->has_fixed_register_binding &&
                 binding->fixed_register_binding_id >=
                     function->fixed_register_binding_count) ||
                (binding->early_clobber &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {
                return false;
            }
            used_indices[binding->operand_index] = true;
            switch (binding->kind) {
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                if (!available_pointer_pointee(
                        function, available_values, binding->value, &pointee) ||
                    (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT &&
                     minic_type_is_const(pointee)) ||
                    !minic_type_unqualified(pointee, &value_type) ||
                    /* A GNU memory operand denotes an addressable storage
                     * region, not necessarily a scalar value. Linux's
                     * __get_user/__put_user uses an intentionally opaque
                     * struct __large_struct memory carrier. Register roles
                     * still require a machine scalar; target admission
                     * determines whether the aggregate memory shape is
                     * legal for a particular instruction family. */
                    ((binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                      binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
                     !minic_type_is_integer(value_type) &&
                     !minic_type_is_pointer(value_type))) {
                    return false;
                }
                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
                    has_memory_readwrite = true;
                }
                break;
            case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
                if (!minic_type_is_integer(function->values[binding->value].type) &&
                    !minic_type_is_pointer(function->values[binding->value].type)) {
                    return false;
                }
                break;
            default:
                return false;
            }
        }
        return !has_memory_readwrite || inline_asm->has_memory_clobber;
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type);
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
        if ((!callee->is_variadic &&
             instruction->value.call.argument_count != callee->parameter_count) ||
            (callee->is_variadic &&
             instruction->value.call.argument_count < callee->parameter_count) ||
            !minic_type_equal(instruction->type, callee->return_type)) {
            return false;
        }
        returns_void = minic_type_is_void(callee->return_type);
        if (returns_void) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID) {
                return false;
            }
        } else if (minic_type_is_record(callee->return_type)) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object >= function->object_count ||
                !minic_type_equal(
                    function->objects[instruction->value.call.result_object].type,
                    callee->return_type)) {
                return false;
            }
        } else if (!minic_core_ir_call_scalar_type(callee->return_type) ||
                   instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
                   !instruction_result_is_valid(function, instruction)) {
            return false;
        }
        argument_end =
            instruction->value.call.argument_begin + instruction->value.call.argument_count;
        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            if (parameter_index >= callee->parameter_count) {
                if (!callee->is_variadic) {
                    return false;
                }
                if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    MinicCoreValueId value_id = argument->value.value_id;

                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !minic_core_ir_call_scalar_type(function->values[value_id].type)) {
                        return false;
                    }
                    continue;
                }
                if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    MinicCoreObjectId object_id = argument->value.object_id;

                    if (object_id >= function->object_count ||
                        !minic_type_is_record(function->objects[object_id].type)) {
                        return false;
                    }
                    continue;
                }
                return false;
            }
            {
                MinicType parameter_type = callee->parameter_types[parameter_index];

                if (minic_core_ir_call_scalar_type(parameter_type)) {
                    MinicCoreValueId value_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                        return false;
                    }
                    value_id = argument->value.value_id;
                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !minic_type_equal(function->values[value_id].type, parameter_type)) {
                        return false;
                    }
                } else if (minic_type_is_record(parameter_type)) {
                    MinicCoreObjectId object_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                        return false;
                    }
                    object_id = argument->value.object_id;
                    if (object_id >= function->object_count ||
                        !minic_type_equal(function->objects[object_id].type, parameter_type)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
        const MinicCoreCallSignature *signature;
        MinicCoreValueId callee_value;
        MinicType function_type;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        callee_value = instruction->value.indirect_call.callee;
        if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
            callee_value >= function->value_count || !available_values[callee_value] ||
            !minic_type_pointee(function->values[callee_value].type, &function_type) ||
            !minic_type_is_function(function_type) ||
            instruction->value.indirect_call.argument_begin > function->call_argument_count ||
            instruction->value.indirect_call.argument_count >
                function->call_argument_count - instruction->value.indirect_call.argument_begin) {
            return false;
        }
        signature =
            &function->call_signatures[instruction->value.indirect_call.signature_id];
        if (function_type.function_type_id != signature->function_type_id ||
            (!signature->is_variadic &&
             instruction->value.indirect_call.argument_count != signature->parameter_count) ||
            (signature->is_variadic &&
             instruction->value.indirect_call.argument_count < signature->parameter_count) ||
            !minic_type_equal(instruction->type, signature->return_type)) {
            return false;
        }
        returns_void = minic_type_is_void(signature->return_type);
        if ((returns_void && instruction->result != MINIC_CORE_VALUE_INVALID) ||
            (!returns_void && !instruction_result_is_valid(function, instruction))) {
            return false;
        }
        argument_end = instruction->value.indirect_call.argument_begin +
                       instruction->value.indirect_call.argument_count;
        for (argument_index = instruction->value.indirect_call.argument_begin;
             argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index =
                argument_index - instruction->value.indirect_call.argument_begin;
            if (parameter_index >= signature->parameter_count) {
                MinicCoreValueId value_id;

                if (!signature->is_variadic ||
                    argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    return false;
                }
                value_id = argument->value.value_id;
                if (value_id >= function->value_count || !available_values[value_id] ||
                    !minic_core_ir_call_scalar_type(function->values[value_id].type)) {
                    return false;
                }
                continue;
            }
            {
                MinicType parameter_type = signature->parameter_types[parameter_index];

                if (minic_core_ir_call_scalar_type(parameter_type)) {
                    MinicCoreValueId value_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                        return false;
                    }
                    value_id = argument->value.value_id;
                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !minic_type_equal(function->values[value_id].type, parameter_type)) {
                        return false;
                    }
                } else if (minic_type_is_record(parameter_type)) {
                    MinicCoreObjectId object_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                        return false;
                    }
                    object_id = argument->value.object_id;
                    if (object_id >= function->object_count ||
                        !minic_type_equal(function->objects[object_id].type, parameter_type)) {
                        return false;
                    }
                } else {
                    return false;
                }
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
        if (minic_type_is_record(function->return_type)) {
            MinicType result_type;
            /* Returning a const-qualified LOCAL record copies its value.
             * C discards top-level qualifiers on the returned rvalue, not
             * nested member or pointee qualifiers. The Core return object
             * can still be the original const local; require exact record
             * identity after removing only that top-level qualifier. */
            return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
                   terminator->return_object < function->object_count &&
                   minic_type_unqualified(
                       function->objects[terminator->return_object].type,
                       &result_type) &&
                   minic_type_equal(result_type, function->return_type);
        }
        return terminator->return_value < function->value_count &&
               available_values[terminator->return_value] &&
               minic_type_equal(function->values[terminator->return_value].type,
                                function->return_type);
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
               terminator->return_object == MINIC_CORE_OBJECT_INVALID;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return terminator->branch_target < function->block_count;
    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_VERIFY: the source
       expression was accepted by GNU goto semantics; Core owns only
       the pointer-valued dynamic control-flow edge. */
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return terminator->indirect_target < function->value_count &&
               available_values[terminator->indirect_target] &&
               minic_type_is_pointer(function->values[terminator->indirect_target].type);
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
        if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_IR_VERIFY_FAIL reason=block-shape function=%s block=%" PRIu32
                " terminator=%d instructions=%zu capacity=%zu\n",
                function->name, block_id, block->has_terminator,
                block->instruction_count, block->instruction_capacity);
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
            if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL)
                (void)fprintf(stderr,
                    "CORE_IR_VERIFY_FAIL reason=instruction-ownership function=%s "
                    "block=%" PRIu32 " instruction=%" PRIu32 "\n",
                    function->name, block_id, instruction_id);
            return false;
        }
        instruction = &function->instructions[instruction_id];
        if (!instruction_is_valid(function, instruction, available_values)) {
            /* Opt-in verifier diagnostics are emitted only on a rejected
             * instruction. Preserve the ordinary verifier's strict contract. */
            if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL) {
                (void)fprintf(stderr,
                    "CORE_IR_VERIFY_FAIL reason=instruction function=%s "
                    "block=%" PRIu32 " id=%" PRIu32 " kind=%d "
                    "result=%" PRIu32 " span=%zu:%zu\n",
                    function->name, block_id, instruction_id,
                    (int)instruction->kind, instruction->result,
                    instruction->span.begin.line,
                    instruction->span.begin.column);
            }
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
    if (!terminator_is_valid(function, &block->terminator, available_values)) {
        if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_IR_VERIFY_FAIL reason=terminator function=%s "
                "block=%" PRIu32 " kind=%d\n",
                function->name, block_id, (int)block->terminator.kind);
        return false;
    }
    return true;
}

bool minic_core_function_verify(const MinicCoreFunction *function) {
    bool *available_values;
    bool *instruction_seen;
    bool *value_seen;
    size_t block_index;
    size_t index;
    bool valid;

    if (function == NULL || function->name == NULL || function->name_length == 0U ||
        !minic_function_calling_convention_valid(function->calling_convention) ||
        (function->parameter_count != 0U && function->parameter_types == NULL) ||
        !storage_shape_is_valid(
            function->globals, function->global_count, function->global_capacity) ||
        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(function->call_signatures,
                                function->call_signature_count,
                                function->call_signature_capacity) ||
        !storage_shape_is_valid(
            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||
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
    for (index = 0U; index < function->object_count; ++index) {
        const MinicCoreObject *object = &function->objects[index];
        if (object->element_count == 0U || minic_type_is_void(object->type) ||
            minic_type_is_function(object->type)) {
            return false;
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        const MinicCoreGlobal *global;

        global = &function->globals[index];
        if (global->name == NULL || global->name_length == 0U ||
            !minic_core_ir_global_addressable_type(global->type)) {
            return false;
        }
    }
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *callee;
        size_t parameter_index;

        callee = &function->callees[index];
        if (callee->name == NULL || callee->name_length == 0U ||
            !minic_function_calling_convention_valid(callee->calling_convention) ||
            !minic_core_ir_direct_call_return_type(callee->return_type) ||
            (callee->parameter_count != 0U && callee->parameter_types == NULL)) {
            return false;
        }
        /* M85B_RECORD_CALLEE_VERIFIER: direct callees share the same
           scalar-or-record parameter contract used by creation and CALL
           instruction verification. Indirect signatures remain scalar-only. */
        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
            if (!minic_core_ir_call_parameter_type(callee->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        const MinicCoreCallSignature *signature;
        size_t parameter_index;

        signature = &function->call_signatures[index];
        if (signature->function_type_id == MINIC_FUNCTION_TYPE_INVALID ||
            !minic_function_calling_convention_valid(signature->calling_convention) ||
            (!minic_type_is_void(signature->return_type) &&
             !minic_core_ir_call_scalar_type(signature->return_type)) ||
            (signature->parameter_count != 0U && signature->parameter_types == NULL)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < signature->parameter_count;
             ++parameter_index) {
            if (!minic_core_ir_call_parameter_type(signature->parameter_types[parameter_index])) {
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
