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
    MinicType void_pointer_type;
    MinicType allocation_parameters[1];
    MinicType release_parameters[1];
    MinicType allocation_function_type;
    MinicType duplicate_allocation_function_type;
    MinicType release_function_type;
    MinicType allocation_pointer_type;
    MinicType release_pointer_type;
    const MinicFunctionType *function_type;
    const MinicRecord *record;
    const MinicRecordField *field;
    char record_name[] = "AES_ctx";
    char array_field_name[] = "RoundKey";
    char pointer_field_name[] = "Next";

    minic_c0_program_initialize(&program);
    integer_type = minic_type_int();
    if (!minic_type_pointer_to(integer_type, &pointer_type) ||
        !minic_type_pointer_to(minic_type_void(), &void_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("pointer type construction");
    }

    allocation_parameters[0] = minic_type_unsigned_long();
    release_parameters[0] = void_pointer_type;
    if (!minic_c0_program_intern_function_type(&program,
                                               void_pointer_type,
                                               allocation_parameters,
                                               1U,
                                               &allocation_function_type) ||
        !minic_c0_program_intern_function_type(&program,
                                               void_pointer_type,
                                               allocation_parameters,
                                               1U,
                                               &duplicate_allocation_function_type) ||
        !minic_c0_program_intern_function_type(&program,
                                               minic_type_void(),
                                               release_parameters,
                                               1U,
                                               &release_function_type) ||
        !minic_type_pointer_to(allocation_function_type, &allocation_pointer_type) ||
        !minic_type_pointer_to(release_function_type, &release_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("function signature construction");
    }
    if (program.function_type_count != 2U ||
        !minic_type_is_function(allocation_function_type) ||
        !minic_type_is_function(release_function_type) ||
        !minic_type_equal(allocation_function_type, duplicate_allocation_function_type) ||
        minic_type_equal(allocation_function_type, release_function_type) ||
        !minic_type_is_pointer(allocation_pointer_type) ||
        !minic_type_is_pointer(release_pointer_type) ||
        minic_type_equal(allocation_pointer_type, release_pointer_type) ||
        minic_type_equal(allocation_pointer_type, void_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("function signature identity");
    }
    function_type = minic_c0_program_function_type(&program, allocation_function_type.function_type_id);
    if (function_type == NULL || function_type->parameter_count != 1U ||
        !minic_type_equal(function_type->return_type, void_pointer_type) ||
        !minic_type_equal(function_type->parameter_types[0], minic_type_unsigned_long())) {
        minic_c0_program_destroy(&program);
        return fail("function signature metadata");
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
            1U) ||
        !minic_c0_record_add_field(
            &program, record_id, "Alloc", 5U, allocation_pointer_type, 1U) ||
        !minic_c0_record_add_field(
            &program, record_id, "Release", 7U, release_pointer_type, 1U)) {
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
        !record->is_complete || record->field_count != 4U ||
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
    field = minic_c0_record_field(record, 2U);
    if (field == NULL || strcmp(field->name, "Alloc") != 0 ||
        !minic_type_equal(field->type, allocation_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("allocation callback field metadata");
    }
    field = minic_c0_record_field(record, 3U);
    if (field == NULL || strcmp(field->name, "Release") != 0 ||
        !minic_type_equal(field->type, release_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("release callback field metadata");
    }
    if (minic_c0_program_record(&program, MINIC_RECORD_INVALID) != NULL ||
        minic_c0_record_field(record, 4U) != NULL ||
        minic_c0_program_function_type(&program, MINIC_FUNCTION_TYPE_INVALID) != NULL) {
        minic_c0_program_destroy(&program);
        return fail("bounds check");
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS frontend/record\n");
    return 0;
}
