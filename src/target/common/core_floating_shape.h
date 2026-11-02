#ifndef MINIC_TARGET_COMMON_CORE_FLOATING_SHAPE_H
#define MINIC_TARGET_COMMON_CORE_FLOATING_SHAPE_H

#include "core/core_ir.h"

/* The entire floating-scalar Core family has one sealed semantic shape.
 * IEEE payload transport, native F/D instructions, softfloat calls and
 * integer word normalization remain machine/ABI decisions. */
typedef struct MinicCoreFloatingShape {
    MinicCoreTypeId result_id;
    MinicCoreTypeId source_id;
    size_t integer_bytes;
    bool integer_signed;
    MinicCoreFloatFormat result_format;
    MinicCoreFloatFormat source_format;
} MinicCoreFloatingShape;

static inline const MinicCoreType *minic_core_floating_base(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    return function != NULL && function->type_context != NULL
        ? minic_core_type_get(function->type_context,
            minic_core_type_unqualified(function->type_context, id)) : NULL;
}

static inline bool minic_core_floating_format(
    const MinicCoreType *type, MinicCoreFloatFormat format) {
    const size_t bytes = format == MINIC_CORE_FLOAT_BINARY32 ? 4U
                       : format == MINIC_CORE_FLOAT_BINARY64 ? 8U
                       : format == MINIC_CORE_FLOAT_EXTENDED ? 16U : 0U;
    return type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT &&
           type->has_layout && bytes != 0U && type->size == bytes &&
           type->value.floating.format == format;
}

static inline bool minic_core_floating_integer(
    const MinicCoreType *type, size_t *bytes, bool *is_signed) {
    if (type == NULL || bytes == NULL || is_signed == NULL ||
        type->kind != MINIC_CORE_TYPE_INTEGER || !type->has_layout ||
        (type->size != 1U && type->size != 2U && type->size != 4U &&
         type->size != 8U && type->size != 16U) ||
        type->value.integer.bit_width != type->size * 8U ||
        (type->value.integer.is_bool && type->size != 1U))
        return false;
    *bytes = type->size;
    *is_signed = type->value.integer.is_signed;
    return true;
}

static inline bool minic_core_floating_shape(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction,
    MinicCoreFloatingShape *shape) {
    const MinicCoreType *result, *source = NULL, *right = NULL;
    MinicCoreValueId operand = MINIC_CORE_VALUE_INVALID;
    MinicCoreFloatFormat expected = MINIC_CORE_FLOAT_BINARY64;
    bool binary = false, compare = false, constant = false;
    bool source_float = false, result_float = false;
    bool input_integer = false, output_integer = false;
    if (function == NULL || function->type_context == NULL ||
        function->values == NULL || instruction == NULL || shape == NULL ||
        instruction->result >= function->value_count ||
        instruction->core_type_id !=
            function->values[instruction->result].core_type_id)
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        constant = true;
        break;
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        expected = MINIC_CORE_FLOAT_BINARY32;
        binary = true;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        binary = true;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        binary = true;
        compare = true;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        break;
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        source_float = true;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        result_float = true;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        input_integer = true;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        output_integer = true;
        break;
    default:
        return false;
    }
    result = minic_core_floating_base(function, instruction->core_type_id);
    if (result == NULL) return false;
    if (constant) {
        if (!minic_core_floating_format(result, MINIC_CORE_FLOAT_BINARY32) &&
            !minic_core_floating_format(result, MINIC_CORE_FLOAT_BINARY64) &&
            !minic_core_floating_format(result, MINIC_CORE_FLOAT_EXTENDED))
            return false;
    } else if (compare) {
        if (result->kind != MINIC_CORE_TYPE_INTEGER || !result->has_layout ||
            result->size != 4U || result->value.integer.bit_width != 32U ||
            !result->value.integer.is_signed || result->value.integer.is_bool)
            return false;
    } else if (!output_integer &&
               !minic_core_floating_format(result,
                    result_float ? MINIC_CORE_FLOAT_BINARY32
                    : binary ? expected : MINIC_CORE_FLOAT_BINARY64)) {
        return false;
    }
    if (!constant) {
        operand = binary ? instruction->value.binary.left
                         : instruction->value.operand;
        if (operand >= function->value_count) return false;
        source = minic_core_floating_base(function,
            function->values[operand].core_type_id);
        if (source == NULL) return false;
        if (binary) {
            const MinicCoreValueId rhs = instruction->value.binary.right;
            if (rhs >= function->value_count) return false;
            right = minic_core_floating_base(function,
                function->values[rhs].core_type_id);
            if (!minic_core_floating_format(source, expected) ||
                !minic_core_floating_format(right, expected))
                return false;
        } else if (!input_integer &&
                   !minic_core_floating_format(source,
                       source_float ? MINIC_CORE_FLOAT_BINARY32
                                    : MINIC_CORE_FLOAT_BINARY64)) {
            return false;
        }
    }
    *shape = (MinicCoreFloatingShape){
        .result_id = instruction->core_type_id,
        .source_id = operand == MINIC_CORE_VALUE_INVALID
            ? MINIC_CORE_TYPE_INVALID : function->values[operand].core_type_id,
        .result_format = result->kind == MINIC_CORE_TYPE_FLOAT
            ? result->value.floating.format : MINIC_CORE_FLOAT_BINARY64,
        .source_format = source != NULL && source->kind == MINIC_CORE_TYPE_FLOAT
            ? source->value.floating.format : MINIC_CORE_FLOAT_BINARY64,
    };
    if (input_integer)
        return minic_core_floating_integer(source, &shape->integer_bytes,
                                           &shape->integer_signed);
    if (output_integer)
        return minic_core_floating_integer(result, &shape->integer_bytes,
                                           &shape->integer_signed);
    return true;
}

#endif
