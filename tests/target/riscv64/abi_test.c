#include "frontend/ast.h"
#include "target/riscv64/abi.h"

#include <stdio.h>

#define CHECK(condition)                                                                         \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                        \
        }                                                                                        \
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
    MinicRiscv64AbiValue value;

    minic_c0_program_initialize(&program);
    record16_fields[0] = minic_type_long();
    record16_fields[1] = minic_type_long();
    record4_field = minic_type_int();
    record_fp_field = minic_type_double();

    CHECK(add_record(&program, record16_fields, 2U, &record16));
    CHECK(add_record(&program, &record4_field, 1U, &record4));
    CHECK(add_record(&program, &record_fp_field, 1U, &record_fp));
    CHECK(minic_type_pointer_to(minic_type_int(), &pointer_type));

    CHECK(expect_value(
        &program, minic_type_int(), MINIC_RISCV64_ABI_VALUE_INTEGER, 4U, 1U));
    CHECK(expect_value(&program, pointer_type, MINIC_RISCV64_ABI_VALUE_INTEGER, 8U, 1U));
    CHECK(expect_value(
        &program, minic_type_double(), MINIC_RISCV64_ABI_VALUE_FLOAT, 8U, 1U));
    CHECK(expect_value(&program, minic_type_void(), MINIC_RISCV64_ABI_VALUE_VOID, 0U, 0U));
    CHECK(expect_value(&program, record16, MINIC_RISCV64_ABI_VALUE_AGGREGATE, 16U, 2U));

    CHECK(!minic_riscv64_abi_classify_value(&program, record4, &value));
    CHECK(!minic_riscv64_abi_classify_value(&program, record_fp, &value));

    minic_c0_program_destroy(&program);
    return true;
}

static bool test_argument_placement(void) {
    MinicC0Program program;
    MinicType record_fields[2];
    MinicType record16;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiCursor before_failure;
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

    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_int(), true, &cursor, &location));
    CHECK(location.integer_register_count == 0U);
    CHECK(location.stack_slot_begin == 1U);
    CHECK(location.stack_slot_count == 1U);

    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_double(), true, &cursor, &location));
    CHECK(location.floating_register_begin == 0U);
    CHECK(location.floating_register_count == 1U);
    CHECK(location.stack_slot_count == 0U);

    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_double(), false, &cursor, &location));
    CHECK(location.floating_register_count == 0U);
    CHECK(location.integer_register_count == 0U);
    CHECK(location.stack_slot_begin == 2U);
    CHECK(location.stack_slot_count == 1U);

    minic_riscv64_abi_cursor_initialize(&cursor);
    for (index = 0U; index < 8U; ++index) {
        CHECK(minic_riscv64_abi_place_argument(
            &program, minic_type_double(), true, &cursor, &location));
        CHECK(location.floating_register_begin == index);
        CHECK(location.floating_register_count == 1U);
    }
    before_failure = cursor;
    CHECK(!minic_riscv64_abi_place_argument(
        &program, minic_type_double(), true, &cursor, &location));
    CHECK(cursor.integer_register_count == before_failure.integer_register_count);
    CHECK(cursor.floating_register_count == before_failure.floating_register_count);
    CHECK(cursor.stack_slot_count == before_failure.stack_slot_count);

    minic_c0_program_destroy(&program);
    return true;
}

static bool test_unsupported_argument_is_transactional(void) {
    MinicC0Program program;
    MinicType field_type;
    MinicType record4;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiCursor before_failure;
    MinicRiscv64AbiArgumentLocation location;

    minic_c0_program_initialize(&program);
    field_type = minic_type_int();
    CHECK(add_record(&program, &field_type, 1U, &record4));

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_long(), true, &cursor, &location));
    before_failure = cursor;
    CHECK(!minic_riscv64_abi_place_argument(&program, record4, true, &cursor, &location));
    CHECK(cursor.integer_register_count == before_failure.integer_register_count);
    CHECK(cursor.floating_register_count == before_failure.floating_register_count);
    CHECK(cursor.stack_slot_count == before_failure.stack_slot_count);

    minic_c0_program_destroy(&program);
    return true;
}

int main(void) {
    if (!test_value_classification() || !test_argument_placement() ||
        !test_unsupported_argument_is_transactional()) {
        return 1;
    }
    (void)puts("PASS rv64 abi classification+placement canonical owner");
    return 0;
}
