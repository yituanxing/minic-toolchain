#include "core/core_ir.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_scalar.h"

#include <stdio.h>

#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)

/* This intentionally bogus target integer model exposes any accidental
 * Core -> C fallback in the attached live path. Standalone legacy tests keep
 * their old model through separate probes. */
static size_t legacy_integer_queries;
bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type, unsigned int *bits) {
    (void)target;
    (void)program;
    (void)type;
    (void)bits;
    ++legacy_integer_queries;
    return false;
}

static bool add_integer(MinicCoreTypeContext *types, unsigned int bits,
                        bool sign, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = ((size_t)bits + 7U) / 8U;
    type.alignment = type.size;
    type.value.integer.bit_width = bits;
    type.value.integer.is_signed = sign;
    return minic_core_type_register(types, &type, id);
}

static bool add_pointer(MinicCoreTypeContext *types, MinicCoreTypeId pointee,
                        MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = pointee;
    return minic_core_type_register(types, &type, id);
}

int main(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId i8, i32, u32, u64, p_i8, p_i32;
    MinicCoreValue values[4] = {0};
    MinicCoreFunction fn = {0};
    MinicCoreInstruction ins = {0};
    MinicCoreIntegerOverflowNarrowShape narrow;
    MinicDirectMachineInstructionShape machine_shape;
    size_t width, lw, rw;
    bool sign, ls, rs, pointer, ptr_to_int, source_pointer, left_signed;
    bool wide_left, right_signed, result_signed;

    minic_core_type_context_initialize(&types);
    REQUIRE(add_integer(&types, 8U, true, &i8));
    REQUIRE(add_integer(&types, 32U, true, &i32));
    REQUIRE(add_integer(&types, 32U, false, &u32));
    REQUIRE(add_integer(&types, 64U, false, &u64));
    REQUIRE(add_pointer(&types, i8, &p_i8));
    REQUIRE(add_pointer(&types, i32, &p_i32));
    REQUIRE(minic_lp64_core_valid_parameter_type(&types, i32));
    REQUIRE(minic_lp64_core_valid_parameter_type(&types, p_i32));
    REQUIRE(minic_lp64_core_valid_return_type(&types, u64));
    REQUIRE(minic_lp64_core_valid_return_with_indirect(&types, i32, NULL));
    REQUIRE(!minic_lp64_core_valid_parameter_type(&types, MINIC_CORE_TYPE_INVALID));

    /* Invalid C descriptors deliberately remain zero in these Core values:
     * live numeric/width admission must require only attached CoreTypeIds. */
    values[0].core_type_id = i32;
    values[1].core_type_id = i32;
    values[2].core_type_id = p_i32;
    values[3].core_type_id = u64;
    fn.type_context = &types;
    fn.values = values;
    fn.value_count = 4U;

    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;
    ins.value.integer_overflow.operator_kind = MINIC_CORE_INTEGER_OVERFLOW_ADD;
    ins.value.integer_overflow.left = 0U;
    ins.value.integer_overflow.right = 1U;
    ins.value.integer_overflow.result_address = 2U;
    REQUIRE(minic_core_integer_overflow_shape(&fn, &ins,
                                               &width, &sign));
    REQUIRE(width == 4U && sign);
    values[1].core_type_id = u64;
    REQUIRE(minic_core_integer_overflow_mixed_add_i32_shape(
        &fn, &ins, &left_signed));
    REQUIRE(left_signed);
    REQUIRE(!minic_core_integer_overflow_shape(&fn, &ins,
                                                &width, &sign));
    values[1].core_type_id = i32;

    /* An enum's Core representation may be an ordinary i8. Lowering must
     * capture the source restriction because checking the C *pointer* for
     * enum origin would always return false. The attached target reads only
     * the source proof, not the untrusted frontend descriptor. */
    values[2].core_type_id = p_i8;
    REQUIRE(minic_core_integer_overflow_narrow_result_shape(
        &fn, &ins, &narrow));
    REQUIRE(narrow.result_width == 1U && narrow.result_signed);
    REQUIRE(minic_type_pointer_to(minic_type_enum(7U), &values[2].type));
    ins.value.integer_overflow.source_overflow_destination_ineligible = true;
    REQUIRE(!minic_core_integer_overflow_narrow_result_shape(
        &fn, &ins, &narrow));
    REQUIRE(!minic_core_integer_overflow_shape(
        &fn, &ins, &width, &sign));
    REQUIRE(legacy_integer_queries == 0U);
    ins.value.integer_overflow.source_overflow_destination_ineligible = false;
    values[2].core_type_id = p_i32;
    ins.value.integer_overflow.source_overflow_destination_ineligible = true;
    REQUIRE(!minic_core_integer_overflow_shape(
        &fn, &ins, &width, &sign));
    ins.value.integer_overflow.source_overflow_destination_ineligible = false;
    REQUIRE(minic_core_integer_overflow_shape(
        &fn, &ins, &width, &sign));

    /* Exercise every Common overflow consumer with poisoned C descriptors.
     * Their successful and rejected shapes are properties of Core alone. */
    values[0].core_type_id = u64;
    values[1].core_type_id = u32;
    REQUIRE(minic_core_integer_overflow_u64_u32_to_i32_shape(
        &fn, &ins, &wide_left) && wide_left);
    values[0].core_type_id = u32;
    values[1].core_type_id = u64;
    REQUIRE(minic_core_integer_overflow_u64_u32_to_i32_shape(
        &fn, &ins, &wide_left) && !wide_left);
    values[0].core_type_id = i32;
    values[1].core_type_id = u32;
    ins.value.integer_overflow.operator_kind = MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY;
    REQUIRE(minic_core_integer_overflow_mixed_multiply_i32_shape(
        &fn, &ins, &left_signed, &right_signed, &result_signed));
    REQUIRE(left_signed && !right_signed && result_signed);
    values[1].core_type_id = i32;
    REQUIRE(!minic_core_integer_overflow_mixed_multiply_i32_shape(
        &fn, &ins, &left_signed, &right_signed, &result_signed));
    values[2].core_type_id = p_i8;
    REQUIRE(minic_core_integer_overflow_narrow_result_shape(&fn, &ins, &narrow));
    fn.type_context = NULL;
    REQUIRE(!minic_core_integer_overflow_narrow_result_shape(&fn, &ins, &narrow));
    REQUIRE(!minic_core_integer_overflow_mixed_multiply_i32_shape(
        &fn, &ins, &left_signed, &right_signed, &result_signed));
    ins.value.integer_overflow.operator_kind = MINIC_CORE_INTEGER_OVERFLOW_ADD;
    REQUIRE(!minic_core_integer_overflow_mixed_add_i32_shape(&fn, &ins, &left_signed));
    REQUIRE(!minic_core_integer_overflow_u64_u32_to_i32_shape(&fn, &ins, &wide_left));
    fn.type_context = &types;
    values[2].core_type_id = p_i32;
    values[1].core_type_id = i32;
    fn.type_context = NULL;
    REQUIRE(!minic_core_integer_overflow_shape(
        &fn, &ins, &width, &sign));
    fn.type_context = &types;

    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CLZ;
    ins.core_type_id = i32;
    values[1].core_type_id = u32;
    ins.value.operand = 1U;
    {
        unsigned int bits;
        REQUIRE(minic_core_integer_bitscan_shape(&fn,
                                                  &ins, &bits));
        REQUIRE(bits == 32U);
        values[1].core_type_id = i32;
        REQUIRE(!minic_core_integer_bitscan_shape(&fn,
                                                   &ins, &bits));
        fn.type_context = NULL;
        REQUIRE(!minic_core_integer_bitscan_shape(&fn, &ins, &bits));
        fn.type_context = &types;
    }

    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
    ins.core_type_id = i32;
    ins.value.operand = 0U;
    REQUIRE(minic_multi_direct_integer_unary_shape(
        &fn, &ins, &width, &sign));
    REQUIRE(width == 4U && sign);

    ins.kind = MINIC_CORE_INSTRUCTION_POINTER_LESS;
    ins.value.binary.left = 2U;
    ins.value.binary.right = 2U;
    REQUIRE(minic_multi_direct_pointer_less_shape(&fn, &ins));

    ins.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    ins.core_type_id = i32;
    ins.value.operand = 2U;
    REQUIRE(minic_multi_direct_scalar_bitcast_shape(
        &fn, &ins, &ptr_to_int, &width,
        &sign, &source_pointer));
    REQUIRE(ptr_to_int && source_pointer && width == 4U && sign);

    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
    ins.value.binary.left = 0U;
    ins.value.binary.right = 1U;
    REQUIRE(minic_multi_direct_integer_shift_shape(
        &fn, &ins, &lw, &rw, &ls, &rs));
    REQUIRE(lw == 4U && rw == 4U && ls && rs);

    ins.kind = MINIC_CORE_INSTRUCTION_LOAD;
    ins.value.load.address = 2U;
    REQUIRE(minic_multi_direct_scalar_load_shape(
        &fn, &ins, &width, &sign, &pointer));
    REQUIRE(width == 4U && sign && !pointer);
    ins.kind = MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    ins.value.atomic_load.address = 2U;
    ins.value.atomic_load.memory_order = 0U;
    REQUIRE(minic_multi_direct_scalar_load_shape(
        &fn, &ins, &width, &sign, &pointer));
    REQUIRE(width == 4U && sign && !pointer);
    ins.value.atomic_load.memory_order = 4U;
    REQUIRE(!minic_multi_direct_scalar_load_shape(
        &fn, &ins, &width, &sign, &pointer));
    REQUIRE(legacy_integer_queries == 0U);

    ins.kind = MINIC_CORE_INSTRUCTION_STORE;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.store.address = 2U;
    ins.value.store.stored_value = 0U;
    REQUIRE(minic_multi_direct_scalar_store_shape(
        &fn, &ins, &width, &sign, &pointer));
    REQUIRE(width == 4U && sign && !pointer);
    ins.kind = MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
    ins.value.atomic_store.address = 2U;
    ins.value.atomic_store.stored_value = 0U;
    ins.value.atomic_store.memory_order = 0U;
    REQUIRE(minic_multi_direct_scalar_store_shape(
        &fn, &ins, &width, &sign, &pointer));
    ins.value.atomic_store.memory_order = 3U;
    REQUIRE(!minic_multi_direct_scalar_store_shape(
        &fn, &ins, &width, &sign, &pointer));

    values[2].core_type_id = MINIC_CORE_TYPE_INVALID;
    ins.kind = MINIC_CORE_INSTRUCTION_LOAD;
    ins.value.load.address = 2U;
    REQUIRE(!minic_multi_direct_scalar_load_shape(
        &fn, &ins, &width, &sign, &pointer));
    values[2].core_type_id = p_i32;

    /* The Direct LP64 machine family uses the same exclusive Core execution
     * view across scalar ALU, compare, pointer offsets and dispatch. */
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    ins.core_type_id = i32;
    ins.result = 0U;
    ins.value.binary.left = 0U;
    ins.value.binary.right = 1U;
    REQUIRE(minic_multi_direct_integer_binary_shape(
        &fn, &ins, &lw, &rw, &ls, &rs));
    REQUIRE(lw == 4U && rw == 4U && ls && rs);
    REQUIRE(minic_multi_direct_machine_instruction_shape(
        &fn, &ins, false, &machine_shape));
    REQUIRE(machine_shape.family == MINIC_DIRECT_MACHINE_INTEGER);
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
    REQUIRE(minic_multi_direct_integer_compare_shape(
        &fn, &ins, &lw, &rw, &ls, &rs));
    ins.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
    ins.value.operand = 2U;
    REQUIRE(minic_multi_direct_scalar_zero_shape(
        &fn, &ins, &width, &sign, &pointer) && pointer);
    ins.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
    ins.value.pointer_offset.base = 2U;
    ins.value.pointer_offset.index = 0U;
    ins.value.pointer_offset.element_size = 4U;
    REQUIRE(minic_multi_direct_pointer_offset_shape(
        &fn, &ins, 16U, &width, &sign) && width == 4U && sign);
    REQUIRE(!minic_multi_direct_pointer_offset_shape(
        &fn, &ins, 2U, &width, &sign));

    fn.type_context = NULL;
    REQUIRE(!minic_multi_direct_pointer_offset_shape(
        &fn, &ins, 16U, &width, &sign));
    ins.kind = MINIC_CORE_INSTRUCTION_LOAD;
    ins.value.load.address = 2U;
    REQUIRE(!minic_multi_direct_scalar_load_shape(
        &fn, &ins, &width, &sign, &pointer));
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    REQUIRE(!minic_multi_direct_integer_binary_shape(
        &fn, &ins, &lw, &rw, &ls, &rs));
    REQUIRE(!minic_multi_direct_machine_instruction_shape(
        &fn, &ins, false, &machine_shape));
    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
    REQUIRE(!minic_multi_direct_machine_instruction_shape(
        &fn, &ins, false, &machine_shape));
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
    REQUIRE(!minic_multi_direct_integer_compare_shape(
        &fn, &ins, &lw, &rw, &ls, &rs));
    ins.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
    REQUIRE(!minic_multi_direct_scalar_zero_shape(
        &fn, &ins, &width, &sign, &pointer));
    minic_core_type_context_destroy(&types);
    (void)puts("CORE_NUMERIC_CORETYPE=PASS overflow enum_source_proof bitscan LP64_scalar bitcast atomic_no_C_query");
    return 0;
}
