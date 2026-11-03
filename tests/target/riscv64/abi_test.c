#include "core/core_type.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/function_abi_plan.h"

#include <stdio.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static bool expect_value(const MinicCoreTypeContext *types,
                         MinicCoreTypeId type,
                         MinicRiscv64AbiValueKind kind,
                         size_t storage_size,
                         size_t slot_count) {
    MinicRiscv64AbiValue value;

    CHECK(minic_riscv64_abi_classify_core_value_profile(types, type, false, &value));
    CHECK(value.kind == kind);
    CHECK(value.storage_size == storage_size);
    CHECK(value.slot_count == slot_count);
    return true;
}

/* The former ABI fixtures constructed C records that the production
 * machine never sees. Build exact Core layouts and exercise the one real
 * classifier and transactional register cursor instead. */
static bool add_integer(MinicCoreTypeContext *types, unsigned int bits,
                        bool sign, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bits / 8U;
    type.alignment = bits == 128U ? 16U : type.size;
    type.value.integer.bit_width = bits;
    type.value.integer.is_signed = sign;
    return minic_core_type_register(types, &type, id);
}
static bool add_float(MinicCoreTypeContext *types, size_t size,
                      MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = size;
    type.alignment = size;
    type.value.floating.format = size == 4U
        ? MINIC_CORE_FLOAT_BINARY32 : MINIC_CORE_FLOAT_BINARY64;
    return minic_core_type_register(types, &type, id);
}
static bool add_void(MinicCoreTypeContext *types, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_VOID;
    return minic_core_type_register(types, &type, id);
}
static bool add_pointer(MinicCoreTypeContext *types,
                        MinicCoreTypeId pointee, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = pointee;
    return minic_core_type_register(types, &type, id);
}
static bool add_record_with_alignment(
    MinicCoreTypeContext *types, const MinicCoreTypeId *field_types,
    size_t count, size_t forced_alignment, MinicCoreTypeId *id) {
    MinicCoreRecordField fields[4] = {0};
    MinicCoreType record = {0};
    size_t i, size = 0U, alignment = 1U;
    CHECK(count <= sizeof(fields) / sizeof(fields[0]));
    for (i = 0U; i < count; ++i) {
        const MinicCoreType *field = minic_core_type_get(types, field_types[i]);
        size_t align;
        CHECK(field != NULL && field->has_layout && field->alignment != 0U);
        align = field->alignment;
        size = (size + align - 1U) & ~(align - 1U);
        fields[i].type_id = field_types[i];
        fields[i].byte_offset = size;
        fields[i].alignment = align;
        size += field->size;
        if (alignment < align) alignment = align;
    }
    if (forced_alignment != 0U) alignment = forced_alignment;
    size = (size + alignment - 1U) & ~(alignment - 1U);
    record.kind = MINIC_CORE_TYPE_RECORD;
    record.has_layout = true;
    record.size = size;
    record.alignment = alignment;
    record.value.record.is_complete = true;
    record.value.record.fields = fields;
    record.value.record.field_count = count;
    return minic_core_type_register(types, &record, id);
}
static bool add_record(MinicCoreTypeContext *types,
                       const MinicCoreTypeId *field_types,
                       size_t field_count, MinicCoreTypeId *id) {
    return add_record_with_alignment(types, field_types, field_count, 0U, id);
}

static bool test_value_classification(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId i32, i64, f64, void_id, i128, u128;
    MinicCoreTypeId record16_fields[2];
    MinicCoreTypeId record4_field;
    MinicCoreTypeId record_fp_field;
    MinicCoreTypeId record16;
    MinicCoreTypeId record4;
    MinicCoreTypeId record_fp;
    MinicCoreTypeId pointer_type;
    MinicRiscv64AbiValue fp_record_value;

    minic_core_type_context_initialize(&types);
    CHECK(add_integer(&types, 32U, true, &i32));
    CHECK(add_integer(&types, 64U, true, &i64));
    CHECK(add_integer(&types, 128U, true, &i128));
    CHECK(add_integer(&types, 128U, false, &u128));
    CHECK(add_float(&types, 8U, &f64));
    CHECK(add_void(&types, &void_id));
    record16_fields[0] = i64;
    record16_fields[1] = i64;
    record4_field = i32;
    record_fp_field = f64;

    CHECK(add_record(&types, record16_fields, 2U, &record16));
    CHECK(add_record(&types, &record4_field, 1U, &record4));
    CHECK(add_record(&types, &record_fp_field, 1U, &record_fp));
    CHECK(add_pointer(&types, i32, &pointer_type));

    CHECK(expect_value(&types, i32, MINIC_RISCV64_ABI_VALUE_INTEGER, 4U, 1U));
    CHECK(expect_value(&types, pointer_type, MINIC_RISCV64_ABI_VALUE_INTEGER, 8U, 1U));
    CHECK(expect_value(&types, f64, MINIC_RISCV64_ABI_VALUE_FLOAT, 8U, 1U));
    CHECK(expect_value(&types, void_id, MINIC_RISCV64_ABI_VALUE_VOID, 0U, 0U));
    CHECK(expect_value(&types, i128,
                       MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR, 16U, 2U));
    CHECK(expect_value(&types, u128,
                       MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR, 16U, 2U));
    CHECK(expect_value(&types, record16, MINIC_RISCV64_ABI_VALUE_AGGREGATE, 16U, 2U));

    CHECK(expect_value(&types, record4, MINIC_RISCV64_ABI_VALUE_AGGREGATE, 4U, 1U));
    CHECK(!minic_riscv64_abi_classify_core_value_profile(
        &types, record_fp, false, &fp_record_value));
    CHECK(minic_riscv64_abi_classify_core_value_profile(
        &types, record_fp, true, &fp_record_value));
    CHECK(fp_record_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          fp_record_value.storage_size == 8U &&
          fp_record_value.slot_count == 1U);

    minic_core_type_context_destroy(&types);
    return true;
}

static bool test_return_argument_cursor(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId i64, f64, u128;
    MinicCoreTypeId record24_fields[3];
    MinicCoreTypeId record_fp_field;
    MinicCoreTypeId record24;
    MinicCoreTypeId record_fp;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    MinicRiscv64AbiArgumentLocation location;

    minic_core_type_context_initialize(&types);
    CHECK(add_integer(&types, 64U, true, &i64));
    CHECK(add_integer(&types, 128U, false, &u128));
    CHECK(add_float(&types, 8U, &f64));
    record24_fields[0] = i64;
    record24_fields[1] = i64;
    record24_fields[2] = i64;
    record_fp_field = f64;
    CHECK(add_record(&types, record24_fields, 3U, &record24));
    CHECK(add_record(&types, &record_fp_field, 1U, &record_fp));

    CHECK(
        minic_riscv64_abi_cursor_initialize_for_core_return_profile(&types, record24, false, &cursor, &return_value));
    CHECK(return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT);
    CHECK(return_value.storage_size == 24U);
    CHECK(cursor.integer_register_count == 1U);
    CHECK(cursor.floating_register_count == 0U);
    CHECK(cursor.stack_slot_count == 0U);
    CHECK(minic_riscv64_abi_place_core_argument(&types, i64, true, &cursor, &location));
    CHECK(location.integer_register_begin == 1U);
    CHECK(location.integer_register_count == 1U);

    CHECK(minic_riscv64_abi_cursor_initialize_for_core_return_profile(
        &types, u128, false, &cursor, &return_value));
    CHECK(return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(return_value.storage_size == 16U && return_value.slot_count == 2U);
    CHECK(cursor.integer_register_count == 0U);
    CHECK(minic_riscv64_abi_cursor_initialize_for_core_return_profile(
        &types, u128, true, &cursor, &return_value));
    CHECK(return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(return_value.slot_count == 2U);

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(!minic_riscv64_abi_cursor_initialize_for_core_return_profile(
        &types, record_fp, false, &cursor, &return_value));
    CHECK(cursor.integer_register_count == 0U);
    CHECK(cursor.floating_register_count == 0U);
    CHECK(cursor.stack_slot_count == 0U);

    minic_core_type_context_destroy(&types);
    return true;
}

static bool test_argument_placement(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId i64, i32, f64, f32, i128, u128;
    MinicCoreTypeId record_fields[2];
    MinicCoreTypeId record16;
    MinicCoreTypeId fp_fields[1];
    MinicCoreTypeId record_fp;
    MinicCoreTypeId aligned_record;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiArgumentLocation location;
    size_t index;

    minic_core_type_context_initialize(&types);
    CHECK(add_integer(&types, 32U, true, &i32));
    CHECK(add_integer(&types, 64U, true, &i64));
    CHECK(add_integer(&types, 128U, true, &i128));
    CHECK(add_integer(&types, 128U, false, &u128));
    CHECK(add_float(&types, 8U, &f64));
    CHECK(add_float(&types, 4U, &f32));
    record_fields[0] = i64;
    record_fields[1] = i64;
    CHECK(add_record(&types, record_fields, 2U, &record16));

    minic_riscv64_abi_cursor_initialize(&cursor);
    for (index = 0U; index < 7U; ++index) {
        CHECK(minic_riscv64_abi_place_core_argument(
            &types, i64, true, &cursor, &location));
        CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER);
        CHECK(location.integer_register_begin == index);
        CHECK(location.integer_register_count == 1U);
        CHECK(location.stack_slot_count == 0U);
    }

    CHECK(minic_riscv64_abi_place_core_argument(&types, record16, true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE);
    CHECK(location.integer_register_begin == 7U);
    CHECK(location.integer_register_count == 1U);
    CHECK(location.stack_slot_begin == 0U);
    CHECK(location.stack_slot_count == 1U);
    CHECK(cursor.integer_register_count == 8U);
    CHECK(cursor.stack_slot_count == 1U);

    CHECK(minic_riscv64_abi_place_core_argument(&types, i32, true, &cursor, &location));
    CHECK(location.integer_register_count == 0U);
    CHECK(location.stack_slot_begin == 1U);
    CHECK(location.stack_slot_count == 1U);

    CHECK(
        minic_riscv64_abi_place_core_argument(&types, f64, true, &cursor, &location));
    CHECK(location.floating_register_begin == 0U);
    CHECK(location.floating_register_count == 1U);
    CHECK(location.stack_slot_count == 0U);

    CHECK(
        minic_riscv64_abi_place_core_argument(&types, f64, false, &cursor, &location));
    CHECK(location.floating_register_count == 0U);
    CHECK(location.integer_register_count == 0U);
    CHECK(location.stack_slot_begin == 2U);
    CHECK(location.stack_slot_count == 1U);

    /* LP64D/hard-float int128 is still a two-XLEN integer. At a7,
     * one half must spill to stack slot zero; soft-float is identical. */
    minic_riscv64_abi_cursor_initialize(&cursor);
    for (index = 0U; index < 7U; ++index)
        CHECK(minic_riscv64_abi_place_core_argument(
            &types, i64, true, &cursor, &location));
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, u128, true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(location.integer_register_begin == 7U &&
          location.integer_register_count == 1U);
    CHECK(location.stack_slot_begin == 0U &&
          location.stack_slot_count == 1U);
    minic_riscv64_abi_cursor_initialize(&cursor);
    cursor.lp64_soft_float = true;
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, i128, true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(location.integer_register_count == 2U &&
          location.stack_slot_count == 0U);

    minic_riscv64_abi_cursor_initialize(&cursor);
    for (index = 0U; index < 8U; ++index) {
        CHECK(minic_riscv64_abi_place_core_argument(
            &types, f64, true, &cursor, &location));
        CHECK(location.floating_register_begin == index);
        CHECK(location.floating_register_count == 1U);
    }
    /* A ninth named FP scalar falls back to the first unused GAR. It does
     * not consume a ninth FAR or fail the entire function admission. */
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, f64, true, &cursor, &location));
    CHECK(location.floating_register_count == 0U);
    CHECK(location.integer_register_begin == 0U &&
          location.integer_register_count == 1U &&
          location.stack_slot_count == 0U);
    CHECK(cursor.floating_register_count == 8U &&
          cursor.integer_register_count == 1U);
    /* Both streams are independent; GAR exhaustion falls through to stack. */
    for (index = 1U; index < 8U; ++index)
        CHECK(minic_riscv64_abi_place_core_argument(
            &types, f64, true, &cursor, &location));
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, f64, true, &cursor, &location));
    CHECK(location.floating_register_count == 0U &&
          location.integer_register_count == 0U &&
          location.stack_slot_begin == 0U &&
          location.stack_slot_count == 1U);

    /* A named LP64D float-containing record needs FP aggregate flattening;
     * an unimplemented shape must never silently become an a0 pointer.
     * Unnamed LP64D variadics instead use the regular integer convention. */
    fp_fields[0] = f32;
    CHECK(add_record(&types, fp_fields, 1U, &record_fp));
    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(!minic_riscv64_abi_place_core_argument(
        &types, record_fp, true, &cursor, &location));
    CHECK(cursor.integer_register_count == 0U &&
          cursor.floating_register_count == 0U &&
          cursor.stack_slot_count == 0U);
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, record_fp, false, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          location.integer_register_begin == 0U &&
          location.integer_register_count == 1U &&
          location.floating_register_count == 0U);
    minic_riscv64_abi_cursor_initialize(&cursor);
    cursor.lp64_soft_float = true;
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, record_fp, true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          location.integer_register_count == 1U &&
          location.floating_register_count == 0U);

    /* The unnamed 16-aligned aggregate must start at an even a-register
     * under both LP64D and LP64, even when the first named integer uses a0. */
    CHECK(add_record_with_alignment(&types, record_fields, 2U, 16U, &aligned_record));
    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, i64, true, &cursor, &location));
    CHECK(minic_riscv64_abi_place_core_argument(
        &types, aligned_record, false, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          location.integer_register_begin == 2U &&
          location.integer_register_count == 2U);

    minic_core_type_context_destroy(&types);
    return true;
}

static bool test_unsupported_argument_is_transactional(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId i64, void_id;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiCursor before_failure;
    MinicRiscv64AbiArgumentLocation location;

    minic_core_type_context_initialize(&types);
    CHECK(add_integer(&types, 64U, true, &i64));
    CHECK(add_void(&types, &void_id));

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_core_argument(&types, i64, true, &cursor, &location));
    before_failure = cursor;
    CHECK(!minic_riscv64_abi_place_core_argument(&types, void_id, true, &cursor, &location));
    CHECK(cursor.integer_register_count == before_failure.integer_register_count);
    CHECK(cursor.floating_register_count == before_failure.floating_register_count);
    CHECK(cursor.stack_slot_count == before_failure.stack_slot_count);

    minic_core_type_context_destroy(&types);
    return true;
}

static bool register_plan_type(MinicCoreTypeContext *types,
                               MinicCoreTypeKind kind,
                               size_t bytes,
                               MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = kind;
    type.has_layout = kind != MINIC_CORE_TYPE_VOID;
    type.size = bytes;
    type.alignment = bytes;
    if (kind == MINIC_CORE_TYPE_FLOAT)
        type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    else if (kind == MINIC_CORE_TYPE_INTEGER) {
        type.value.integer.bit_width = (unsigned int)(bytes * 8U);
        type.value.integer.is_signed = true;
    }
    return minic_core_type_register(types, &type, id);
}

/* Real function-plan admission: the owner must reject unsupported
 * placements and roll back its allocation rather than relying on the
 * downstream RV64 opcode verifier to discover them. */
static bool test_function_abi_plan_qualification(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction fn = {0};
    MinicRiscv64FunctionAbiPlan plan = {0};
    MinicCoreTypeId params[9];
    MinicCoreTypeId void_id, double_id, long_id, record24_id;
    MinicCoreRecordField record_fields[3] = {0};
    MinicCoreType record24 = {0};
    size_t i;

    minic_core_type_context_initialize(&types);
    CHECK(register_plan_type(&types, MINIC_CORE_TYPE_VOID, 0U, &void_id));
    CHECK(register_plan_type(&types, MINIC_CORE_TYPE_FLOAT, 8U, &double_id));
    CHECK(register_plan_type(&types, MINIC_CORE_TYPE_INTEGER, 8U, &long_id));
    fn.core_return_type_id = void_id;
    fn.core_parameter_type_ids = params;
    fn.parameter_count = 8U;
    for (i = 0U; i < 9U; ++i) params[i] = double_id;
    /* Standalone plans must also be sealed: legacy function types do not
     * authorize an untyped ABI decision. */
    CHECK(!minic_riscv64_function_abi_plan_build_profile(&fn, &plan, false));
    CHECK(!plan.prepared && plan.parameter_locations == NULL);
    fn.type_context = &types;
    CHECK(minic_riscv64_function_abi_plan_build_profile(&fn, &plan, false));
    CHECK(plan.prepared && plan.parameter_location_count == 8U);
    CHECK(plan.parameter_locations[7].floating_register_begin == 7U);
    CHECK(plan.parameter_locations[7].floating_register_count == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);

    /* RV64 LP64D named FP9 is legal: fa0..fa7, then a0. */
    fn.parameter_count = 9U;
    CHECK(minic_riscv64_function_abi_plan_build_profile(&fn, &plan, false));
    CHECK(plan.prepared && plan.parameter_location_count == 9U);
    CHECK(plan.parameter_locations[8].floating_register_count == 0U &&
          plan.parameter_locations[8].integer_register_begin == 0U &&
          plan.parameter_locations[8].integer_register_count == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);

    /* The same value is legal under soft-float LP64 as a GPR argument. */
    fn.parameter_count = 1U;
    CHECK(minic_riscv64_function_abi_plan_build_profile(&fn, &plan, true));
    CHECK(plan.named_parameter_cursor.lp64_soft_float);
    CHECK(plan.parameter_locations[0].floating_register_count == 0U);
    CHECK(plan.parameter_locations[0].integer_register_count == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);

    for (i = 0U; i < 3U; ++i) {
        record_fields[i].type_id = long_id;
        record_fields[i].byte_offset = i * 8U;
        record_fields[i].alignment = 8U;
    }
    record24.kind = MINIC_CORE_TYPE_RECORD;
    record24.has_layout = true;
    record24.size = 24U;
    record24.alignment = 8U;
    record24.value.record.fields = record_fields;
    record24.value.record.field_count = 3U;
    record24.value.record.is_complete = true;
    CHECK(minic_core_type_register(&types, &record24, &record24_id));
    fn.core_return_type_id = record24_id;
    fn.parameter_count = 1U;
    params[0] = long_id;
    CHECK(minic_riscv64_function_abi_plan_build_profile(&fn, &plan, false));
    CHECK(plan.return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT);
    CHECK(plan.parameter_locations[0].integer_register_begin == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);

    /* Rejected Core argument placement rolls back the plan allocation. */
    params[0] = void_id;
    CHECK(!minic_riscv64_function_abi_plan_build_profile(&fn, &plan, false));
    CHECK(!plan.prepared && plan.parameter_locations == NULL);
    params[0] = long_id;
    CHECK(minic_riscv64_function_abi_plan_build_profile(&fn, &plan, false));
    minic_riscv64_function_abi_plan_destroy(&plan);
    minic_core_type_context_destroy(&types);
    return true;
}

int main(void) {
    if (!test_value_classification() || !test_return_argument_cursor() ||
        !test_argument_placement() || !test_unsupported_argument_is_transactional() ||
        !test_function_abi_plan_qualification()) {
        return 1;
    }
    (void)puts("PASS rv64 abi classification+return-cursor+placement canonical owner");
    return 0;
}
