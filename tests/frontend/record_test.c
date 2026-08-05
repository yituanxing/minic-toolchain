#include "frontend/ast.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "FAIL frontend/record: %s\n", message);
    return 1;
}

int main(void)
{
    MinicC0Program program;
    MinicRecordId record_id;
    MinicRecordId duplicate_id;
    MinicType integer_type;
    MinicType pointer_type;
    const MinicRecord *record;
    const MinicRecordField *field;
    char record_name[] = "AES_ctx";
    char array_field_name[] = "RoundKey";
    char pointer_field_name[] = "Next";

    minic_c0_program_initialize(&program);
    integer_type = minic_type_int();
    if (!minic_type_pointer_to(integer_type, &pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("pointer type construction");
    }

    if (!minic_c0_program_add_record(
            &program,
            record_name,
            strlen(record_name),
            &record_id)) {
        minic_c0_program_destroy(&program);
        return fail("record insertion");
    }
    record_name[0] = 'X';

    if (!minic_c0_record_add_field(
            &program,
            record_id,
            array_field_name,
            strlen(array_field_name),
            integer_type,
            176U) ||
        !minic_c0_record_add_field(
            &program,
            record_id,
            pointer_field_name,
            strlen(pointer_field_name),
            pointer_type,
            1U)) {
        minic_c0_program_destroy(&program);
        return fail("field insertion");
    }
    array_field_name[0] = 'X';
    pointer_field_name[0] = 'X';

    if (minic_c0_record_add_field(
            &program,
            record_id,
            "RoundKey",
            8U,
            integer_type,
            1U) ||
        minic_c0_record_add_field(
            &program,
            record_id,
            "Empty",
            5U,
            integer_type,
            0U)) {
        minic_c0_program_destroy(&program);
        return fail("invalid field accepted");
    }
    if (!minic_c0_program_finish_record(&program, record_id) ||
        minic_c0_program_finish_record(&program, record_id) ||
        minic_c0_record_add_field(
            &program,
            record_id,
            "Late",
            4U,
            integer_type,
            1U)) {
        minic_c0_program_destroy(&program);
        return fail("record completion boundary");
    }
    if (minic_c0_program_add_record(
            &program,
            "AES_ctx",
            7U,
            &duplicate_id)) {
        minic_c0_program_destroy(&program);
        return fail("duplicate record accepted");
    }

    record = minic_c0_program_record(&program, record_id);
    if (record == NULL || strcmp(record->name, "AES_ctx") != 0 ||
        !record->is_complete || record->field_count != 2U ||
        record->storage_size != 0U || record->alignment != 0U) {
        minic_c0_program_destroy(&program);
        return fail("record metadata");
    }
    field = minic_c0_record_field(record, 0U);
    if (field == NULL || strcmp(field->name, "RoundKey") != 0 ||
        field->element_count != 176U ||
        !minic_type_equal(field->type, integer_type)) {
        minic_c0_program_destroy(&program);
        return fail("array field metadata");
    }
    field = minic_c0_record_field(record, 1U);
    if (field == NULL || strcmp(field->name, "Next") != 0 ||
        field->element_count != 1U ||
        !minic_type_equal(field->type, pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("pointer field metadata");
    }
    if (minic_c0_program_record(&program, MINIC_RECORD_INVALID) != NULL ||
        minic_c0_record_field(record, 2U) != NULL) {
        minic_c0_program_destroy(&program);
        return fail("bounds check");
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS frontend/record\n");
    return 0;
}
