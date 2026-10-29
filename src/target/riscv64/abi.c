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
            /* LP64D classifies an aggregate containing FP leaves using
             * register flattening (faN and sometimes aN), NOT by passing
             * a pointer. Until the complete mixed FP/GP location model is
             * implemented, reject that named/return shape rather than
             * silently emitting a GNU-incompatible indirect argument.
             * LP64 and unnamed LP64D arguments use the integer convention. */
            if (!lp64_soft_float &&
                !minic_common_aggregate_integral_tree(program, type))
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

bool minic_riscv64_classify_abi_value(
    const MinicC0Program *program, MinicType type,
    MinicRiscv64AbiValue *result) {
    return minic_riscv64_classify_abi_value_profile(
        program, type, false, result);
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

bool minic_riscv64_abi_place_argument(const MinicC0Program *program,
                                      MinicType type,
                                      bool is_fixed_parameter,
                                      MinicRiscv64AbiCursor *cursor,
                                      MinicRiscv64AbiArgumentLocation *location) {
    MinicRiscv64AbiArgumentLocation result;
    MinicRiscv64AbiCursor next;
    size_t integer_slots;
    bool requires_even_pair = false;
    MinicRiscvGprAbiCursor gpr;
    MinicRiscvGprAbiPlacement placed;

    if (cursor == NULL || location == NULL ||
        cursor->integer_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        cursor->floating_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        !minic_riscv64_abi_classify_value_profile(
            program, type, cursor->lp64_soft_float || !is_fixed_parameter,
            &result.value) ||
        result.value.kind == MINIC_RISCV64_ABI_VALUE_VOID) {
        return false;
    }

    result.integer_register_begin = cursor->integer_register_count;
    result.integer_register_count = 0U;
    result.floating_register_begin = cursor->floating_register_count;
    result.floating_register_count = 0U;
    result.stack_slot_begin = cursor->stack_slot_count;
    result.stack_slot_count = 0U;
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
            ? result.value.slot_count
            : 1U;
    /* LP64 requires paired GPRs for 16-byte-aligned unnamed 2*XLEN.
     * Named wide values retain a7/stack0 split; LP64D FP is handled above. */
    if (!is_fixed_parameter && integer_slots == 2U) {
        size_t size, alignment;
        if (!minic_data_layout_type(minic_default_data_layout(), program, type,
                                    &size, &alignment))
            return false;
        requires_even_pair = alignment >= 16U;
    }
    gpr.register_words = next.integer_register_count;
    gpr.stack_words = next.stack_slot_count;
    gpr.variadic_stack_only = next.integer_register_count == 8U &&
                              next.stack_slot_count != 0U;
    if (!minic_riscv_gpr_abi_place(
            &gpr, integer_slots, !is_fixed_parameter,
            requires_even_pair, &placed))
        return false;
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
