#ifndef MINIC_TARGET_COMMON_CORE_SCALAR_CONVERSION_H
#define MINIC_TARGET_COMMON_CORE_SCALAR_CONVERSION_H

#include "core/core_ir.h"

/* Exact storage gates shared by Core scalar conversions and ABI value views.
 * These do not impose a machine register class or an ABI word width. */
static inline bool minic_core_scalar_integer_storage_valid(
    const MinicCoreType *type) {
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->has_layout &&
           (type->size == 1U || type->size == 2U || type->size == 4U ||
            type->size == 8U || type->size == 16U) &&
           type->value.integer.bit_width == type->size * 8U &&
           (!type->value.integer.is_bool || type->size == 1U);
}

static inline bool minic_core_scalar_pointer_storage_valid(
    const MinicCoreType *type) {
    return type != NULL && type->kind == MINIC_CORE_TYPE_POINTER &&
           type->has_layout && (type->size == 4U || type->size == 8U);
}

/* Shared language-independent integer conversion shape. The Core verifier
 * owns SSA legality; target ABI/frame code owns one-vs-two-register storage,
 * normalization instructions and physical register selection. Neither side
 * may reopen the source C type after a CoreTypeContext is attached. */
typedef struct MinicCoreIntegerConversionShape {
    MinicCoreTypeId source_id;
    MinicCoreTypeId target_id;
    size_t source_bytes;
    size_t target_bytes;
    bool source_signed;
    bool target_signed;
    bool target_bool;
} MinicCoreIntegerConversionShape;

static inline bool minic_core_integer_conversion_shape(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction,
    MinicCoreIntegerConversionShape *shape) {
    const MinicCoreType *source, *target;
    MinicCoreTypeId source_id;
    if (function == NULL || function->type_context == NULL ||
        function->values == NULL || instruction == NULL || shape == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION ||
        instruction->value.operand >= function->value_count ||
        instruction->result >= function->value_count ||
        instruction->core_type_id !=
            function->values[instruction->result].core_type_id)
        return false;
    source_id = function->values[instruction->value.operand].core_type_id;
    source = minic_core_type_base(function->type_context, source_id);
    target = minic_core_type_base(function->type_context, instruction->core_type_id);
    if (!minic_core_scalar_integer_storage_valid(source) ||
        !minic_core_scalar_integer_storage_valid(target))
        return false;
    *shape = (MinicCoreIntegerConversionShape){
        .source_id = source_id,
        .target_id = instruction->core_type_id,
        .source_bytes = source->size,
        .target_bytes = target->size,
        .source_signed = source->value.integer.is_signed,
        .target_signed = target->value.integer.is_signed,
        .target_bool = target->value.integer.is_bool,
    };
    return true;
}

/* A Core scalar bitcast changes the interpretation of a pointer or integer
 * value.  This is the source-language-free contract; no ABI word geometry,
 * pointer sign extension or machine normalization is chosen here.  Backends
 * may reject shapes they cannot physically lower. */
typedef struct MinicCoreScalarBitcastShape {
    MinicCoreTypeId source_id;
    MinicCoreTypeId target_id;
    size_t source_bytes;
    size_t target_bytes;
    bool source_pointer;
    bool target_pointer;
    bool source_signed;
    bool target_signed;
    bool target_bool;
} MinicCoreScalarBitcastShape;

static inline bool minic_core_scalar_bitcast_shape(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction,
    MinicCoreScalarBitcastShape *shape) {
    const MinicCoreType *source, *target;
    MinicCoreTypeId source_id;
    if (function == NULL || function->type_context == NULL ||
        function->values == NULL || instruction == NULL || shape == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||
        instruction->value.operand >= function->value_count ||
        instruction->result >= function->value_count ||
        instruction->core_type_id !=
            function->values[instruction->result].core_type_id)
        return false;
    source_id = function->values[instruction->value.operand].core_type_id;
    source = minic_core_type_base(function->type_context, source_id);
    target = minic_core_type_base(function->type_context, instruction->core_type_id);
    if (!((minic_core_scalar_pointer_storage_valid(source) &&
           (minic_core_scalar_pointer_storage_valid(target) ||
            minic_core_scalar_integer_storage_valid(target))) ||
          (minic_core_scalar_integer_storage_valid(source) &&
           minic_core_scalar_pointer_storage_valid(target))))
        return false;
    *shape = (MinicCoreScalarBitcastShape){
        .source_id = source_id,
        .target_id = instruction->core_type_id,
        .source_bytes = source->size,
        .target_bytes = target->size,
        .source_pointer = source->kind == MINIC_CORE_TYPE_POINTER,
        .target_pointer = target->kind == MINIC_CORE_TYPE_POINTER,
        .source_signed = source->kind == MINIC_CORE_TYPE_INTEGER &&
                         source->value.integer.is_signed,
        .target_signed = target->kind == MINIC_CORE_TYPE_INTEGER &&
                         target->value.integer.is_signed,
        .target_bool = target->kind == MINIC_CORE_TYPE_INTEGER &&
                       target->value.integer.is_bool,
    };
    return true;
}

#endif
