#include "frontend/ast.h"
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

static bool expect_value(const MinicC0Program *program,
                         MinicType type,
                         MinicRiscv64AbiValueKind kind,
                         size_t storage_size,
                         size_t slot_count) {
    MinicRiscv64AbiValue value;

    CHECK(minic_riscv64_abi_classify_value(program, type, &value));
    CHECK(value.kind == kind);
    CHECK(value.storage_size == storage_size);
    CHECK(value.slot_count == slot_count);
    return true;
}

static bool add_record(MinicC0Program *program,
                       const MinicType *field_types,
                       size_t field_count,
                       MinicType *type) {
    static const char *const field_names[] = {"a", "b", "c", "d"};
    MinicRecordId record_id;
    size_t index;

    CHECK(program != NULL);
    CHECK(type != NULL);
    CHECK(field_count <= sizeof(field_names) / sizeof(field_names[0]));
    CHECK(minic_c0_program_add_anonymous_record(program, &record_id));
    for (index = 0U; index < field_count; ++index) {
        CHECK(minic_c0_record_add_field(
            program, record_id, field_names[index], 1U, field_types[index], 1U));
    }
    CHECK(minic_c0_program_finish_record(program, record_id));
    *type = minic_type_record(record_id);
    return true;
}

static bool test_value_classification(void) {
    MinicC0Program program;
    MinicType record16_fields[2];
    MinicType record4_field;
    MinicType record_fp_field;
    MinicType record16;
    MinicType record4;
    MinicType record_fp;
    MinicType pointer_type;
    MinicRiscv64AbiValue fp_record_value;

    minic_c0_program_initialize(&program);
    record16_fields[0] = minic_type_long();
    record16_fields[1] = minic_type_long();
    record4_field = minic_type_int();
    record_fp_field = minic_type_double();

    CHECK(add_record(&program, record16_fields, 2U, &record16));
    CHECK(add_record(&program, &record4_field, 1U, &record4));
    CHECK(add_record(&program, &record_fp_field, 1U, &record_fp));
    CHECK(minic_type_pointer_to(minic_type_int(), &pointer_type));

    CHECK(expect_value(&program, minic_type_int(), MINIC_RISCV64_ABI_VALUE_INTEGER, 4U, 1U));
    CHECK(expect_value(&program, pointer_type, MINIC_RISCV64_ABI_VALUE_INTEGER, 8U, 1U));
    CHECK(expect_value(&program, minic_type_double(), MINIC_RISCV64_ABI_VALUE_FLOAT, 8U, 1U));
    CHECK(expect_value(&program, minic_type_void(), MINIC_RISCV64_ABI_VALUE_VOID, 0U, 0U));
    CHECK(expect_value(&program, minic_type_int128(),
                       MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR, 16U, 2U));
    CHECK(expect_value(&program, minic_type_unsigned_int128(),
                       MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR, 16U, 2U));
    CHECK(expect_value(&program, record16, MINIC_RISCV64_ABI_VALUE_AGGREGATE, 16U, 2U));

    CHECK(expect_value(&program, record4, MINIC_RISCV64_ABI_VALUE_AGGREGATE, 4U, 1U));
    CHECK(!minic_riscv64_abi_classify_value(
        &program, record_fp, &fp_record_value));
    CHECK(minic_riscv64_abi_classify_value_profile(
        &program, record_fp, true, &fp_record_value));
    CHECK(fp_record_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          fp_record_value.storage_size == 8U &&
          fp_record_value.slot_count == 1U);

    minic_c0_program_destroy(&program);
    return true;
}

static bool test_return_argument_cursor(void) {
    MinicC0Program program;
    MinicType record24_fields[3];
    MinicType record_fp_field;
    MinicType record24;
    MinicType record_fp;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    MinicRiscv64AbiArgumentLocation location;

    minic_c0_program_initialize(&program);
    record24_fields[0] = minic_type_long();
    record24_fields[1] = minic_type_long();
    record24_fields[2] = minic_type_long();
    record_fp_field = minic_type_double();
    CHECK(add_record(&program, record24_fields, 3U, &record24));
    CHECK(add_record(&program, &record_fp_field, 1U, &record_fp));

    CHECK(
        minic_riscv64_abi_cursor_initialize_for_return(&program, record24, &cursor, &return_value));
    CHECK(return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT);
    CHECK(return_value.storage_size == 24U);
    CHECK(cursor.integer_register_count == 1U);
    CHECK(cursor.floating_register_count == 0U);
    CHECK(cursor.stack_slot_count == 0U);
    CHECK(minic_riscv64_abi_place_argument(&program, minic_type_long(), true, &cursor, &location));
    CHECK(location.integer_register_begin == 1U);
    CHECK(location.integer_register_count == 1U);

    CHECK(minic_riscv64_abi_cursor_initialize_for_return(
        &program, minic_type_unsigned_int128(), &cursor, &return_value));
    CHECK(return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(return_value.storage_size == 16U && return_value.slot_count == 2U);
    CHECK(cursor.integer_register_count == 0U);
    CHECK(minic_riscv64_abi_cursor_initialize_for_return_profile(
        &program, minic_type_unsigned_int128(), true, &cursor, &return_value));
    CHECK(return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(return_value.slot_count == 2U);

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(!minic_riscv64_abi_cursor_initialize_for_return(
        &program, record_fp, &cursor, &return_value));
    CHECK(cursor.integer_register_count == 0U);
    CHECK(cursor.floating_register_count == 0U);
    CHECK(cursor.stack_slot_count == 0U);

    minic_c0_program_destroy(&program);
    return true;
}

static bool test_argument_placement(void) {
    MinicC0Program program;
    MinicType record_fields[2];
    MinicType record16;
    MinicType fp_fields[1];
    MinicType record_fp;
    MinicType aligned_record;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiArgumentLocation location;
    size_t index;

    minic_c0_program_initialize(&program);
    record_fields[0] = minic_type_long();
    record_fields[1] = minic_type_long();
    CHECK(add_record(&program, record_fields, 2U, &record16));

    minic_riscv64_abi_cursor_initialize(&cursor);
    for (index = 0U; index < 7U; ++index) {
        CHECK(minic_riscv64_abi_place_argument(
            &program, minic_type_long(), true, &cursor, &location));
        CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER);
        CHECK(location.integer_register_begin == index);
        CHECK(location.integer_register_count == 1U);
        CHECK(location.stack_slot_count == 0U);
    }

    CHECK(minic_riscv64_abi_place_argument(&program, record16, true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE);
    CHECK(location.integer_register_begin == 7U);
    CHECK(location.integer_register_count == 1U);
    CHECK(location.stack_slot_begin == 0U);
    CHECK(location.stack_slot_count == 1U);
    CHECK(cursor.integer_register_count == 8U);
    CHECK(cursor.stack_slot_count == 1U);

    CHECK(minic_riscv64_abi_place_argument(&program, minic_type_int(), true, &cursor, &location));
    CHECK(location.integer_register_count == 0U);
    CHECK(location.stack_slot_begin == 1U);
    CHECK(location.stack_slot_count == 1U);

    CHECK(
        minic_riscv64_abi_place_argument(&program, minic_type_double(), true, &cursor, &location));
    CHECK(location.floating_register_begin == 0U);
    CHECK(location.floating_register_count == 1U);
    CHECK(location.stack_slot_count == 0U);

    CHECK(
        minic_riscv64_abi_place_argument(&program, minic_type_double(), false, &cursor, &location));
    CHECK(location.floating_register_count == 0U);
    CHECK(location.integer_register_count == 0U);
    CHECK(location.stack_slot_begin == 2U);
    CHECK(location.stack_slot_count == 1U);

    /* LP64D/hard-float int128 is still a two-XLEN integer. At a7,
     * one half must spill to stack slot zero; soft-float is identical. */
    minic_riscv64_abi_cursor_initialize(&cursor);
    for (index = 0U; index < 7U; ++index)
        CHECK(minic_riscv64_abi_place_argument(
            &program, minic_type_long(), true, &cursor, &location));
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_unsigned_int128(), true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(location.integer_register_begin == 7U &&
          location.integer_register_count == 1U);
    CHECK(location.stack_slot_begin == 0U &&
          location.stack_slot_count == 1U);
    minic_riscv64_abi_cursor_initialize(&cursor);
    cursor.lp64_soft_float = true;
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_int128(), true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR);
    CHECK(location.integer_register_count == 2U &&
          location.stack_slot_count == 0U);

    minic_riscv64_abi_cursor_initialize(&cursor);
    for (index = 0U; index < 8U; ++index) {
        CHECK(minic_riscv64_abi_place_argument(
            &program, minic_type_double(), true, &cursor, &location));
        CHECK(location.floating_register_begin == index);
        CHECK(location.floating_register_count == 1U);
    }
    /* A ninth named FP scalar falls back to the first unused GAR. It does
     * not consume a ninth FAR or fail the entire function admission. */
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_double(), true, &cursor, &location));
    CHECK(location.floating_register_count == 0U);
    CHECK(location.integer_register_begin == 0U &&
          location.integer_register_count == 1U &&
          location.stack_slot_count == 0U);
    CHECK(cursor.floating_register_count == 8U &&
          cursor.integer_register_count == 1U);
    /* Both streams are independent; GAR exhaustion falls through to stack. */
    for (index = 1U; index < 8U; ++index)
        CHECK(minic_riscv64_abi_place_argument(
            &program, minic_type_double(), true, &cursor, &location));
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_double(), true, &cursor, &location));
    CHECK(location.floating_register_count == 0U &&
          location.integer_register_count == 0U &&
          location.stack_slot_begin == 0U &&
          location.stack_slot_count == 1U);

    /* A named LP64D float-containing record needs FP aggregate flattening;
     * an unimplemented shape must never silently become an a0 pointer.
     * Unnamed LP64D variadics instead use the regular integer convention. */
    fp_fields[0] = minic_type_float();
    CHECK(add_record(&program, fp_fields, 1U, &record_fp));
    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(!minic_riscv64_abi_place_argument(
        &program, record_fp, true, &cursor, &location));
    CHECK(cursor.integer_register_count == 0U &&
          cursor.floating_register_count == 0U &&
          cursor.stack_slot_count == 0U);
    CHECK(minic_riscv64_abi_place_argument(
        &program, record_fp, false, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          location.integer_register_begin == 0U &&
          location.integer_register_count == 1U &&
          location.floating_register_count == 0U);
    minic_riscv64_abi_cursor_initialize(&cursor);
    cursor.lp64_soft_float = true;
    CHECK(minic_riscv64_abi_place_argument(
        &program, record_fp, true, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          location.integer_register_count == 1U &&
          location.floating_register_count == 0U);

    /* The unnamed 16-aligned aggregate must start at an even a-register
     * under both LP64D and LP64, even when the first named integer uses a0. */
    CHECK(add_record(&program, record_fields, 2U, &aligned_record));
    program.records[aligned_record.record_id].explicit_alignment = 16U;
    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_long(), true, &cursor, &location));
    CHECK(minic_riscv64_abi_place_argument(
        &program, aligned_record, false, &cursor, &location));
    CHECK(location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
          location.integer_register_begin == 2U &&
          location.integer_register_count == 2U);

    minic_c0_program_destroy(&program);
    return true;
}

static bool test_unsupported_argument_is_transactional(void) {
    MinicC0Program program;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiCursor before_failure;
    MinicRiscv64AbiArgumentLocation location;

    minic_c0_program_initialize(&program);

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_argument(&program, minic_type_long(), true, &cursor, &location));
    before_failure = cursor;
    CHECK(!minic_riscv64_abi_place_argument(&program, minic_type_void(), true, &cursor, &location));
    CHECK(cursor.integer_register_count == before_failure.integer_register_count);
    CHECK(cursor.floating_register_count == before_failure.floating_register_count);
    CHECK(cursor.stack_slot_count == before_failure.stack_slot_count);

    minic_c0_program_destroy(&program);
    return true;
}

/* Real function-plan admission: the owner must reject unsupported
 * placements and roll back its allocation rather than relying on the
 * downstream RV64 opcode verifier to discover them. */
static bool test_function_abi_plan_qualification(void) {
    MinicC0Program program;
    MinicCoreFunction fn = {0};
    MinicRiscv64FunctionAbiPlan plan = {0};
    MinicType params[9];
    MinicType long_fields[3];
    MinicType record24;
    size_t i;

    minic_c0_program_initialize(&program);
    fn.return_type = minic_type_void();
    fn.parameter_types = params;
    fn.parameter_count = 8U;
    for (i = 0U; i < 9U; ++i) params[i] = minic_type_double();
    CHECK(minic_riscv64_function_abi_plan_build_profile(&program, &fn, &plan, false));
    CHECK(plan.prepared && plan.parameter_location_count == 8U);
    CHECK(plan.parameter_locations[7].floating_register_begin == 7U);
    CHECK(plan.parameter_locations[7].floating_register_count == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);

    /* RV64 LP64D named FP9 is legal: fa0..fa7, then a0. */
    fn.parameter_count = 9U;
    CHECK(minic_riscv64_function_abi_plan_build_profile(&program, &fn, &plan, false));
    CHECK(plan.prepared && plan.parameter_location_count == 9U);
    CHECK(plan.parameter_locations[8].floating_register_count == 0U &&
          plan.parameter_locations[8].integer_register_begin == 0U &&
          plan.parameter_locations[8].integer_register_count == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);

    /* The same value is legal under soft-float LP64 as a GPR argument. */
    fn.parameter_count = 1U;
    CHECK(minic_riscv64_function_abi_plan_build_profile(&program, &fn, &plan, true));
    CHECK(plan.named_parameter_cursor.lp64_soft_float);
    CHECK(plan.parameter_locations[0].floating_register_count == 0U);
    CHECK(plan.parameter_locations[0].integer_register_count == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);

    long_fields[0] = minic_type_long();
    long_fields[1] = minic_type_long();
    long_fields[2] = minic_type_long();
    CHECK(add_record(&program, long_fields, 3U, &record24));
    fn.return_type = record24;
    fn.parameter_count = 1U;
    params[0] = minic_type_long();
    CHECK(minic_riscv64_function_abi_plan_build_profile(&program, &fn, &plan, false));
    CHECK(plan.return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT);
    CHECK(plan.parameter_locations[0].integer_register_begin == 1U);
    minic_riscv64_function_abi_plan_destroy(&plan);
    minic_c0_program_destroy(&program);
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
