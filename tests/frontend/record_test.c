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
    MinicRecordId hooks_record_id;
    MinicType integer_type;
    MinicType pointer_type;
    MinicType void_pointer_type;
    MinicType alloc_parameter_types[1];
    MinicType free_parameter_types[1];
    MinicType alloc_function_type;
    MinicType duplicate_alloc_function_type;
    MinicType free_function_type;
    MinicType qualified_function_type;
    MinicType alloc_pointer_type;
    MinicType free_pointer_type;
    const MinicFunctionType *function_type;
    const MinicRecord *record;
    const MinicRecord *hooks_record;
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

    alloc_parameter_types[0] = minic_type_unsigned_long();
    free_parameter_types[0] = void_pointer_type;
    if (!minic_c0_program_add_function_type(&program,
                                            void_pointer_type,
                                            alloc_parameter_types,
                                            1U,
                                            &alloc_function_type) ||
        !minic_c0_program_add_function_type(&program,
                                            void_pointer_type,
                                            alloc_parameter_types,
                                            1U,
                                            &duplicate_alloc_function_type) ||
        !minic_c0_program_add_function_type(&program,
                                            minic_type_void(),
                                            free_parameter_types,
                                            1U,
                                            &free_function_type)) {
        minic_c0_program_destroy(&program);
        return fail("function type insertion");
    }
    if (!minic_type_is_function(alloc_function_type) ||
        !minic_type_equal(alloc_function_type, duplicate_alloc_function_type) ||
        minic_type_equal(alloc_function_type, free_function_type) ||
        program.function_type_count != 2U) {
        minic_c0_program_destroy(&program);
        return fail("function type interning");
    }
    if (minic_type_add_const(alloc_function_type, &qualified_function_type)) {
        minic_c0_program_destroy(&program);
        return fail("qualified function type accepted");
    }
    qualified_function_type = alloc_function_type;
    qualified_function_type.base_qualifiers = MINIC_TYPE_QUALIFIER_CONST;
    if (minic_type_is_function(qualified_function_type)) {
        minic_c0_program_destroy(&program);
        return fail("qualified function type classified as valid");
    }
    function_type = minic_c0_program_function_type(
        &program, alloc_function_type.function_type_id);
    if (function_type == NULL || function_type->parameter_count != 1U ||
        !minic_type_equal(function_type->return_type, void_pointer_type) ||
        !minic_type_equal(function_type->parameter_types[0], minic_type_unsigned_long())) {
        minic_c0_program_destroy(&program);
        return fail("function signature metadata");
    }
    if (!minic_type_pointer_to(alloc_function_type, &alloc_pointer_type) ||
        !minic_type_pointer_to(free_function_type, &free_pointer_type) ||
        !minic_type_is_pointer(alloc_pointer_type) ||
        !minic_type_is_pointer(free_pointer_type) ||
        minic_type_is_function(alloc_pointer_type) ||
        minic_type_equal(alloc_pointer_type, free_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("function pointer identity");
    }
    if (!minic_c0_program_add_record(&program, "Hooks", 5U, &hooks_record_id) ||
        !minic_c0_record_add_field(
            &program, hooks_record_id, "alloc", 5U, alloc_pointer_type, 1U) ||
        !minic_c0_record_add_field(
            &program, hooks_record_id, "free_fn", 7U, free_pointer_type, 1U) ||
        !minic_c0_program_finish_record(&program, hooks_record_id)) {
        minic_c0_program_destroy(&program);
        return fail("function pointer record insertion");
    }
    hooks_record = minic_c0_program_record(&program, hooks_record_id);
    if (hooks_record == NULL || hooks_record->field_count != 2U ||
        !minic_type_equal(hooks_record->fields[0].type, alloc_pointer_type) ||
        !minic_type_equal(hooks_record->fields[1].type, free_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("function pointer record metadata");
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS frontend/record\n");
    return 0;
}
