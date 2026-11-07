#ifndef MINIC_TARGET_COMMON_CORE_ABI_VALUE_SHAPE_H
#define MINIC_TARGET_COMMON_CORE_ABI_VALUE_SHAPE_H

#include "core/core_ir.h"

/* The ABI planner owns placement; machine call/parameter/return emitters
 * consume only sealed Core value shape. Source MinicType descriptors are not
 * an alternate execution-type authority, including on failed Core lookups. */
typedef struct MinicCoreAbiValueShape {
    MinicCoreTypeKind kind;
    size_t bytes;
    size_t integer_width;
    bool integer_signed;
    bool integer_bool;
    MinicCoreFloatFormat float_format;
} MinicCoreAbiValueShape;

static inline bool minic_core_abi_value_shape(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    MinicCoreAbiValueShape *shape) {
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || shape == NULL)
        return false;
    type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, id));
    if (type == NULL) return false;
    *shape = (MinicCoreAbiValueShape){
        .kind = type->kind,
        .bytes = type->size,
    };
    switch (type->kind) {
    case MINIC_CORE_TYPE_VOID:
        return true;
    case MINIC_CORE_TYPE_RECORD:
        return type->has_layout && type->value.record.is_complete;
    case MINIC_CORE_TYPE_POINTER:
        return type->has_layout &&
               (type->size == 4U || type->size == 8U);
    case MINIC_CORE_TYPE_INTEGER:
        if (!type->has_layout ||
            (type->size != 1U && type->size != 2U &&
             type->size != 4U && type->size != 8U &&
             type->size != 16U) ||
            type->value.integer.bit_width != type->size * 8U ||
            (type->value.integer.is_bool && type->size != 1U))
            return false;
        shape->integer_width = type->size;
        shape->integer_signed = type->value.integer.is_signed;
        shape->integer_bool = type->value.integer.is_bool;
        return true;
    case MINIC_CORE_TYPE_FLOAT:
        if (!type->has_layout ||
            (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32
                ? type->size != 4U
                : type->value.floating.format == MINIC_CORE_FLOAT_BINARY64
                    ? type->size != 8U
                    : minic_core_float_format_is_wide(type->value.floating.format)
                        ? type->size != 16U : true))
            return false;
        shape->float_format = type->value.floating.format;
        return true;
    case MINIC_CORE_TYPE_VECTOR:
        return type->has_layout &&
               type->value.vector.representation ==
                   MINIC_CORE_VECTOR_REGISTER_VALUE &&
               (type->size == 8U || type->size == 16U) &&
               type->alignment == type->size;
    default:
        return false;
    }
}

static inline bool minic_core_abi_kind(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    MinicCoreTypeKind kind) {
    MinicCoreAbiValueShape shape;
    return minic_core_abi_value_shape(fn, id, &shape) && shape.kind == kind;
}

static inline bool minic_core_abi_float_format(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    MinicCoreFloatFormat format) {
    MinicCoreAbiValueShape shape;
    return minic_core_abi_value_shape(fn, id, &shape) &&
           shape.kind == MINIC_CORE_TYPE_FLOAT && shape.float_format == format;
}

static inline bool minic_core_abi_wide_scalar(
    const MinicCoreFunction *fn, MinicCoreTypeId id) {
    MinicCoreAbiValueShape shape;
    return minic_core_abi_value_shape(fn, id, &shape) &&
           ((shape.kind == MINIC_CORE_TYPE_INTEGER && shape.bytes == 16U) ||
            (shape.kind == MINIC_CORE_TYPE_FLOAT &&
             minic_core_float_format_is_wide(shape.float_format)));
}

#endif
