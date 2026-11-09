#include "target/riscv32/rv32_internal.h"

static const MinicCoreType *rv32_core_scalar_type(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    MinicCoreTypeId base;
    if (function == NULL || function->type_context == NULL) return NULL;
    base = minic_core_type_unqualified(function->type_context, id);
    return minic_core_type_get(function->type_context, base);
}

bool rv32_core_scalar_storage_words(const MinicCoreFunction *function,
                                    MinicCoreTypeId id, unsigned *words) {
    const MinicCoreType *type = rv32_core_scalar_type(function, id);
    if (type == NULL || words == NULL || !type->has_layout) return false;
    switch (type->kind) {
    case MINIC_CORE_TYPE_INTEGER:
        if ((type->value.integer.is_bool && type->size == 1U) ||
            (type->size == 1U && type->value.integer.bit_width == 8U) ||
            (type->size == 2U && type->value.integer.bit_width == 16U) ||
            (type->size == 4U && type->value.integer.bit_width == 32U)) {
            *words = 1U;
            return true;
        }
        if (type->value.integer.bit_width == 64U && type->size == 8U) {
            *words = 2U;
            return true;
        }
        return false;
    case MINIC_CORE_TYPE_POINTER:
        if (type->size != 4U) return false;
        *words = 1U;
        return true;
    case MINIC_CORE_TYPE_FLOAT:
        if (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 &&
            type->size == 4U) {
            *words = 1U;
            return true;
        }
        if (type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 &&
            type->size == 8U) {
            *words = 2U;
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool rv32_core_scalar(const MinicCoreFunction *function, MinicCoreTypeId id,
                      unsigned *bits, bool *sign) {
    const MinicCoreType *type = rv32_core_scalar_type(function, id);
    if (type == NULL || bits == NULL || sign == NULL || !type->has_layout)
        return false;
    if (type->kind == MINIC_CORE_TYPE_POINTER && type->size == 4U) {
        *bits = 32U;
        *sign = false;
        return true;
    }
    if (type->kind == MINIC_CORE_TYPE_FLOAT &&
        type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 &&
        type->size == 4U) {
        *bits = 32U;
        *sign = false;
        return true;
    }
    if (type->kind != MINIC_CORE_TYPE_INTEGER ||
        type->size > 4U || type->value.integer.bit_width == 0U ||
        type->value.integer.bit_width > 32U) return false;
    *bits = type->value.integer.is_bool ? 1U : type->value.integer.bit_width;
    *sign = type->value.integer.is_signed;
    return *bits == 1U || *bits == 8U || *bits == 16U || *bits == 32U;
}

/* Sealed Core type geometry is still needed by the live ILP32 ABI planner.
 * These queries classify shape only; no GNU text emitter remains here. */
bool rv32_core_is_wide_integer(const MinicCoreFunction *function,
                               MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_scalar_type(function, id);
    return type != NULL && type->has_layout &&
           type->kind == MINIC_CORE_TYPE_INTEGER && type->size == 8U &&
           type->value.integer.bit_width == 64U &&
           !type->value.integer.is_bool;
}

bool rv32_core_unsigned_integer(const MinicCoreFunction *function,
                                MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_scalar_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           !type->value.integer.is_signed && !type->value.integer.is_bool;
}

bool rv32_core_integer_type(const MinicCoreFunction *function,
                            MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_scalar_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER;
}

bool rv32_core_pointer_type(const MinicCoreFunction *function,
                            MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_scalar_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_POINTER &&
           type->has_layout && type->size == 4U;
}

MinicCoreTypeId rv32_core_pointee_type(const MinicCoreFunction *function,
                                      MinicCoreTypeId id) {
    const MinicCoreType *type = rv32_core_scalar_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_POINTER &&
        type->has_layout && type->size == 4U
        ? type->value.pointer.pointee : MINIC_CORE_TYPE_INVALID;
}
