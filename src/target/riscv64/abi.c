#include "target/riscv64/abi.h"
#include "target/common/aggregate_shape.h"
#include "target/riscv/gpr_abi_cursor.h"

const char *const minic_riscv64_abi_integer_argument_registers[8] = {
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
};

const char *const minic_riscv64_abi_floating_argument_registers[8] = {
    "fa0",
    "fa1",
    "fa2",
    "fa3",
    "fa4",
    "fa5",
    "fa6",
    "fa7",
};


#include "target/data_layout.h"

#include <stdint.h>

/* LP64D's integer-convention record path is a psABI classification, not a
 * structural property of common DataLayout. Unions are opaque integer ABI
 * leaves even if one alternative is floating-point; never flatten them.
 * A genuine FP struct/array leaf remains in the separate, not-yet-supported
 * FPR-aggregate path and must continue to fail closed. */
static bool rv64_integer_convention_record_tree(const MinicC0Program *program,
                                                 MinicType type,
                                                 unsigned int depth) {
    const MinicRecord *record;
    const MinicArrayType *array;
    size_t index;

    if (program == NULL || depth > 256U) return false;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) return true;
    if (minic_type_is_array(type)) {
        array = minic_c0_program_array_type(program, type.array_type_id);
        return array != NULL && rv64_integer_convention_record_tree(
            program, array->element_type, depth + 1U);
    }
    if (!minic_type_is_record(type)) return false;
    record = minic_c0_program_record(program, type.record_id);
    if (record == NULL || !record->is_complete) return false;
    if (record->is_union) return true;
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field = minic_c0_record_field(record, index);
        if (field == NULL || !rv64_integer_convention_record_tree(
                program, field->type, depth + 1U)) return false;
    }
    return true;
}

bool minic_riscv64_classify_abi_value_profile(
    const MinicC0Program *program, MinicType type, bool lp64_soft_float,
    MinicRiscv64AbiValue *result) {
    size_t alignment;
    size_t size;

    if (program == NULL || result == NULL) {
        return false;
    }
    result->kind = MINIC_RISCV64_ABI_VALUE_INVALID;
    result->storage_size = 0U;
    result->register_chunks = 0U;
    result->slot_count = 0U;
    if (minic_type_is_void(type)) {
        result->kind = MINIC_RISCV64_ABI_VALUE_VOID;
        return true;
    }
    if (minic_type_is_record(type)) {
        MinicAggregateStorageShape shape;
        if (!minic_common_aggregate_storage_shape(
                program, minic_default_data_layout(), type, 8U, &shape))
            return false;
        size = shape.bytes;
        alignment = shape.alignment;
    } else if (!minic_data_layout_type(
                   minic_default_data_layout(), program, type,
                   &size, &alignment)) {
        return false;
    }
    (void)alignment;

    result->storage_size = size;

    if (minic_type_is_record(type)) {
        if (size == 0U) {
            result->kind = MINIC_RISCV64_ABI_VALUE_IGNORE;
            return true;
        }
        if (size <= 16U) {
            /* LP64D flattens genuine FP struct leaves into FPRs. That
             * placement is not yet implemented, so keep it rejected.
             * Unions (including nested fields) are opaque, non-flattened
             * integer-convention values; LP64 and unnamed LP64D arguments
             * likewise use integer registers/stack without this filter. */
            if (!lp64_soft_float &&
                !rv64_integer_convention_record_tree(program, type, 0U))
                return false;
            result->kind = MINIC_RISCV64_ABI_VALUE_AGGREGATE;
            result->register_chunks = (size + 7U) / 8U;
            return true;
        }
        result->kind = MINIC_RISCV64_ABI_VALUE_INDIRECT;
        return true;
    }

    if (minic_type_is_float(type) || minic_type_is_double(type)) {
        result->kind = MINIC_RISCV64_ABI_VALUE_FLOAT;
        result->register_chunks = 1U;
        return true;
    }
    /* __int128 always occupies two XLEN words under both LP64 and LP64D.
     * The hard/soft FLOAT ABI selects FPRs for float/double, not the size
     * of an integer. The old soft-float-only condition silently reduced
     * hard-float __int128 CALL returns/arguments to a single a-register. */
    if ((minic_type_is_long_double(type) ||
         minic_type_is_int128_integer(type)) && size == 16U) {
        result->kind = MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR;
        result->register_chunks = 2U;
        return true;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        result->kind = MINIC_RISCV64_ABI_VALUE_INTEGER;
        result->register_chunks = 1U;
        return true;
    }
    return false;
}

#define MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT 8U

void minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor) {
    if (cursor == NULL) {
        return;
    }
    cursor->integer_register_count = 0U;
    cursor->floating_register_count = 0U;
    cursor->stack_slot_count = 0U;
    cursor->lp64_soft_float = false;
}

bool minic_riscv64_abi_cursor_initialize_for_return_profile(
    const MinicC0Program *program, MinicType return_type,
    bool lp64_soft_float, MinicRiscv64AbiCursor *cursor,
    MinicRiscv64AbiValue *return_value) {
    MinicRiscv64AbiCursor next;
    MinicRiscv64AbiValue result;

    if (cursor == NULL || return_value == NULL ||
        !minic_riscv64_abi_classify_value_profile(
            program, return_type, lp64_soft_float, &result)) {
        return false;
    }
    minic_riscv64_abi_cursor_initialize(&next);
    next.lp64_soft_float = lp64_soft_float;
    if (result.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        if (!minic_type_is_record(return_type) || result.storage_size <= 16U) {
            return false;
        }
        next.integer_register_count = 1U;
    }
    *cursor = next;
    *return_value = result;
    return true;
}

bool minic_riscv64_abi_cursor_initialize_for_return(
    const MinicC0Program *program, MinicType return_type,
    MinicRiscv64AbiCursor *cursor, MinicRiscv64AbiValue *return_value) {
    return minic_riscv64_abi_cursor_initialize_for_return_profile(
        program, return_type, false, cursor, return_value);
}

bool minic_riscv64_abi_variadic_prefix_from_cursor(
    const MinicRiscv64AbiCursor *cursor, size_t *integer_parameter_count,
    size_t *fixed_stack_slots) {
    if (cursor == NULL || integer_parameter_count == NULL || fixed_stack_slots == NULL ||
        cursor->integer_register_count > 8U ||
        (cursor->stack_slot_count != 0U && cursor->integer_register_count != 8U))
        return false;
    *integer_parameter_count = cursor->integer_register_count;
    *fixed_stack_slots = cursor->stack_slot_count;
    return true;
}

bool minic_riscv64_abi_classify_value_profile(
    const MinicC0Program *program, MinicType type, bool lp64_soft_float,
    MinicRiscv64AbiValue *value) {
    if (!minic_riscv64_classify_abi_value_profile(
            program, type, lp64_soft_float, value)) {
        return false;
    }
    if (value->kind == MINIC_RISCV64_ABI_VALUE_VOID ||
        value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        value->slot_count = 0U;
    } else if (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
               value->kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
        value->slot_count = value->register_chunks;
    } else {
        value->slot_count = 1U;
    }
    return true;
}

bool minic_riscv64_abi_classify_value(
    const MinicC0Program *program, MinicType type,
    MinicRiscv64AbiValue *value) {
    return minic_riscv64_abi_classify_value_profile(
        program, type, false, value);
}

/* A real LP64D floating aggregate requires a dedicated FPR aggregate
 * lowering path. Unions are opaque integer convention leaves, while structs
 * and arrays with floating leaves must still reject conservatively. */
static bool rv64_core_integer_convention_record_tree(
    const MinicCoreTypeContext *types, MinicCoreTypeId id,
    unsigned int depth) {
    const MinicCoreType *type;
    size_t i;
    if (depth > 256U ||
        (id = minic_core_type_unqualified(types, id)) == MINIC_CORE_TYPE_INVALID ||
        (type = minic_core_type_get(types, id)) == NULL) return false;
    if (type->kind == MINIC_CORE_TYPE_INTEGER ||
        type->kind == MINIC_CORE_TYPE_POINTER) return true;
    if (type->kind == MINIC_CORE_TYPE_ARRAY)
        return rv64_core_integer_convention_record_tree(
            types, type->value.array.element, depth + 1U);
    if (type->kind != MINIC_CORE_TYPE_RECORD ||
        !type->value.record.is_complete) return false;
    if (type->value.record.is_union) return true;
    if (type->value.record.field_count != 0U &&
        type->value.record.fields == NULL) return false;
    for (i = 0U; i < type->value.record.field_count; ++i)
        if (!rv64_core_integer_convention_record_tree(
                types, type->value.record.fields[i].type_id, depth + 1U))
            return false;
    return true;
}

bool minic_riscv64_abi_classify_core_value_profile(
    const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    bool lp64_soft_float, MinicRiscv64AbiValue *result) {
    const MinicCoreType *type, *base;
    MinicRiscv64AbiValue value = {0};
    if (types == NULL || result == NULL ||
        (type = minic_core_type_get(types, type_id)) == NULL ||
        (base = minic_core_type_get(
            types, minic_core_type_unqualified(types, type_id))) == NULL)
        return false;
    value.kind = MINIC_RISCV64_ABI_VALUE_INVALID;
    if (base->kind == MINIC_CORE_TYPE_VOID) {
        value.kind = MINIC_RISCV64_ABI_VALUE_VOID;
        *result = value;
        return true;
    }
    if (!type->has_layout || type->alignment == 0U ||
        (type->alignment & (type->alignment - 1U)) != 0U) return false;
    if (base->kind == MINIC_CORE_TYPE_RECORD) {
        MinicAggregateStorageShape shape;
        if (!minic_common_aggregate_storage_shape_core(
                types, type_id, 8U, &shape)) return false;
        value.storage_size = shape.bytes;
        if (shape.bytes == 0U) {
            value.kind = MINIC_RISCV64_ABI_VALUE_IGNORE;
        } else if (shape.bytes <= 16U) {
            if (!lp64_soft_float &&
                !rv64_core_integer_convention_record_tree(types, type_id, 0U))
                return false;
            value.kind = MINIC_RISCV64_ABI_VALUE_AGGREGATE;
            value.register_chunks = shape.words;
        } else {
            value.kind = MINIC_RISCV64_ABI_VALUE_INDIRECT;
        }
    } else {
        value.storage_size = type->size;
        if (base->kind == MINIC_CORE_TYPE_FLOAT &&
            (base->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
             base->value.floating.format == MINIC_CORE_FLOAT_BINARY64)) {
            if ((base->value.floating.format == MINIC_CORE_FLOAT_BINARY32 &&
                 type->size != 4U) ||
                (base->value.floating.format == MINIC_CORE_FLOAT_BINARY64 &&
                 type->size != 8U)) return false;
            value.kind = MINIC_RISCV64_ABI_VALUE_FLOAT;
            value.register_chunks = 1U;
        } else if (((base->kind == MINIC_CORE_TYPE_FLOAT &&
                     base->value.floating.format == MINIC_CORE_FLOAT_EXTENDED) ||
                    (base->kind == MINIC_CORE_TYPE_INTEGER &&
                     base->value.integer.bit_width == 128U)) && type->size == 16U) {
            value.kind = MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR;
            value.register_chunks = 2U;
        } else if (base->kind == MINIC_CORE_TYPE_POINTER && type->size == 8U) {
            value.kind = MINIC_RISCV64_ABI_VALUE_INTEGER;
            value.register_chunks = 1U;
        } else if (base->kind == MINIC_CORE_TYPE_INTEGER &&
                   type->size >= 1U && type->size <= 8U &&
                   base->value.integer.bit_width == type->size * 8U) {
            value.kind = MINIC_RISCV64_ABI_VALUE_INTEGER;
            value.register_chunks = 1U;
        } else return false;
    }
    value.slot_count = value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE
        ? 0U
        : (value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
           value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR)
            ? value.register_chunks : 1U;
    *result = value;
    return true;
}

bool minic_riscv64_abi_cursor_initialize_for_core_return_profile(
    const MinicCoreTypeContext *types, MinicCoreTypeId return_type,
    bool lp64_soft_float, MinicRiscv64AbiCursor *cursor,
    MinicRiscv64AbiValue *return_value) {
    MinicRiscv64AbiCursor next;
    MinicRiscv64AbiValue classified;
    if (cursor == NULL || return_value == NULL ||
        !minic_riscv64_abi_classify_core_value_profile(
            types, return_type, lp64_soft_float, &classified)) return false;
    minic_riscv64_abi_cursor_initialize(&next);
    next.lp64_soft_float = lp64_soft_float;
    if (classified.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        const MinicCoreType *base = minic_core_type_get(
            types, minic_core_type_unqualified(types, return_type));
        if (base == NULL || base->kind != MINIC_CORE_TYPE_RECORD ||
            classified.storage_size <= 16U) return false;
        next.integer_register_count = 1U;
    }
    *cursor = next;
    *return_value = classified;
    return true;
}

/* The register cursor is independent of the source language. Legacy C and
 * resolved Core paths supply the same classified ABI value and alignment. */
static bool rv64_place_classified_argument(
    const MinicRiscv64AbiValue *classified, size_t alignment,
    bool is_fixed_parameter, MinicRiscv64AbiCursor *cursor,
    MinicRiscv64AbiArgumentLocation *location) {
    MinicRiscv64AbiArgumentLocation result = {0};
    MinicRiscv64AbiCursor next;
    size_t integer_slots;
    bool requires_even_pair;
    MinicRiscvGprAbiCursor gpr;
    MinicRiscvGprAbiPlacement placed;
    if (classified == NULL || cursor == NULL || location == NULL ||
        alignment == 0U ||
        cursor->integer_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        cursor->floating_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        classified->kind == MINIC_RISCV64_ABI_VALUE_INVALID ||
        classified->kind == MINIC_RISCV64_ABI_VALUE_VOID) return false;
    result.value = *classified;
    result.integer_register_begin = cursor->integer_register_count;
    result.floating_register_begin = cursor->floating_register_count;
    result.stack_slot_begin = cursor->stack_slot_count;
    next = *cursor;
    if (result.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        *location = result;
        return true;
    }
    if (!cursor->lp64_soft_float && is_fixed_parameter &&
        result.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
        next.floating_register_count < MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT) {
        result.floating_register_begin = next.floating_register_count;
        result.floating_register_count = 1U;
        next.floating_register_count += 1U;
        *cursor = next;
        *location = result;
        return true;
    }
    integer_slots =
        (result.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
         result.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR)
            ? result.value.slot_count : 1U;
    requires_even_pair = !is_fixed_parameter && integer_slots == 2U &&
                         alignment >= 16U;
    gpr.register_words = next.integer_register_count;
    gpr.stack_words = next.stack_slot_count;
    gpr.variadic_stack_only = next.integer_register_count == 8U &&
                              next.stack_slot_count != 0U;
    if (!minic_riscv_gpr_abi_place(&gpr, integer_slots, !is_fixed_parameter,
                                  requires_even_pair, &placed)) return false;
    result.integer_register_begin = placed.register_begin;
    result.integer_register_count = placed.register_count;
    result.stack_slot_begin = placed.stack_begin;
    result.stack_slot_count = placed.stack_count;
    next.integer_register_count = gpr.register_words;
    next.stack_slot_count = gpr.stack_words;
    *cursor = next;
    *location = result;
    return true;
}

bool minic_riscv64_abi_place_core_argument(
    const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    bool is_fixed_parameter, MinicRiscv64AbiCursor *cursor,
    MinicRiscv64AbiArgumentLocation *location) {
    const MinicCoreType *type = minic_core_type_get(types, type_id);
    MinicRiscv64AbiValue classified;
    if (type == NULL || cursor == NULL ||
        !minic_riscv64_abi_classify_core_value_profile(
            types, type_id, cursor->lp64_soft_float || !is_fixed_parameter,
            &classified)) return false;
    return rv64_place_classified_argument(
        &classified, type->alignment == 0U ? 1U : type->alignment,
        is_fixed_parameter, cursor, location);
}

bool minic_riscv64_abi_place_argument(const MinicC0Program *program,
                                      MinicType type,
                                      bool is_fixed_parameter,
                                      MinicRiscv64AbiCursor *cursor,
                                      MinicRiscv64AbiArgumentLocation *location) {
    MinicRiscv64AbiValue classified;
    size_t size, alignment = 1U;
    if (cursor == NULL ||
        !minic_riscv64_abi_classify_value_profile(
            program, type, cursor->lp64_soft_float || !is_fixed_parameter,
            &classified)) return false;
    if (!is_fixed_parameter && classified.slot_count == 2U &&
        !minic_data_layout_type(minic_default_data_layout(), program, type,
                                &size, &alignment)) return false;
    return rv64_place_classified_argument(
        &classified, alignment, is_fixed_parameter, cursor, location);
}
