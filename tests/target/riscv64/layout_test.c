#include "frontend/ast.h"
#include "frontend/type.h"
#include "target/riscv64/layout.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "FAIL target/riscv64/layout: %s\n", message);
    return 1;
}

int main(void)
{
    MinicC0Program program;
    MinicBlockId block_id;
    MinicFunctionId function_id;
    MinicLocalId local_id;
    MinicRecordId record_id;
    MinicRecordId floating_record_id;
    MinicRecordId hooks_record_id;
    MinicLocal local;
    MinicType byte_type;
    MinicType pointer_type;
    MinicType record_type;
    MinicType void_pointer_type;
    MinicType alloc_parameter_types[1];
    MinicType free_parameter_types[1];
    MinicType alloc_function_type;
    MinicType free_function_type;
    MinicType alloc_pointer_type;
    MinicType free_pointer_type;
    MinicDiagnostic diagnostic;
    const MinicFunction *function;
    const MinicRecord *record;
    const MinicRecord *floating_record;
    const MinicRecord *hooks_record;
    size_t byte_size;
    size_t byte_alignment;
    size_t long_size;
    size_t long_alignment;
    size_t double_size;
    size_t double_alignment;
    size_t function_pointer_size;
    size_t function_pointer_alignment;

    minic_c0_program_initialize(&program);
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    byte_type = minic_type_unsigned_char();

    if (!minic_riscv64_type_layout(
            &program,
            byte_type,
            &byte_size,
            &byte_alignment) ||
        byte_size != 1U || byte_alignment != 1U) {
        minic_c0_program_destroy(&program);
        return fail("unsigned-char scalar layout");
    }

    if (!minic_riscv64_type_layout(
            &program,
            minic_type_long(),
            &long_size,
            &long_alignment) ||
        long_size != 8U || long_alignment != 8U ||
        !minic_riscv64_type_layout(
            &program,
            minic_type_unsigned_long(),
            &long_size,
            &long_alignment) ||
        long_size != 8U || long_alignment != 8U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 long scalar layout");
    }

    if (!minic_riscv64_type_layout(
            &program,
            minic_type_double(),
            &double_size,
            &double_alignment) ||
        double_size != 8U || double_alignment != 8U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 double scalar layout");
    }

    if (!minic_type_pointer_to(minic_type_void(), &void_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("construct void pointer type");
    }
    alloc_parameter_types[0] = minic_type_unsigned_long();
    free_parameter_types[0] = void_pointer_type;
    if (!minic_c0_program_add_function_type(&program,
                                            void_pointer_type,
                                            alloc_parameter_types,
                                            1U,
                                            &alloc_function_type) ||
        !minic_c0_program_add_function_type(&program,
                                            minic_type_void(),
                                            free_parameter_types,
                                            1U,
                                            &free_function_type) ||
        !minic_type_pointer_to(alloc_function_type, &alloc_pointer_type) ||
        !minic_type_pointer_to(free_function_type, &free_pointer_type) ||
        !minic_riscv64_type_layout(&program,
                                   alloc_pointer_type,
                                   &function_pointer_size,
                                   &function_pointer_alignment) ||
        function_pointer_size != 8U || function_pointer_alignment != 8U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 function pointer scalar layout");
    }
    if (!minic_c0_program_add_record(&program, "Hooks", 5U, &hooks_record_id) ||
        !minic_c0_record_add_field(
            &program, hooks_record_id, "alloc", 5U, alloc_pointer_type, 1U) ||
        !minic_c0_record_add_field(
            &program, hooks_record_id, "free_fn", 7U, free_pointer_type, 1U) ||
        !minic_c0_program_finish_record(&program, hooks_record_id)) {
        minic_c0_program_destroy(&program);
        return fail("construct function pointer record");
    }

    if (!minic_c0_program_add_record(
            &program,
            "FloatPacket",
            11U,
            &floating_record_id) ||
        !minic_c0_record_add_field(
            &program,
            floating_record_id,
            "prefix",
            6U,
            byte_type,
            1U) ||
        !minic_c0_record_add_field(
            &program,
            floating_record_id,
            "value",
            5U,
            minic_type_double(),
            1U) ||
        !minic_c0_record_add_field(
            &program,
            floating_record_id,
            "suffix",
            6U,
            byte_type,
            1U) ||
        !minic_c0_program_finish_record(&program, floating_record_id)) {
        minic_c0_program_destroy(&program);
        return fail("construct double record");
    }

    if (!minic_c0_program_add_record(
            &program,
            "Packet",
            6U,
            &record_id) ||
        !minic_c0_record_add_field(
            &program,
            record_id,
            "prefix",
            6U,
            byte_type,
            1U) ||
        !minic_c0_record_add_field(
            &program,
            record_id,
            "data",
            4U,
            minic_type_int(),
            4U) ||
        !minic_c0_record_add_field(
            &program,
            record_id,
            "value",
            5U,
            byte_type,
            1U) ||
        !minic_c0_program_finish_record(&program, record_id)) {
        minic_c0_program_destroy(&program);
        return fail("construct record");
    }
    record_type = minic_type_record(record_id);

    if (!minic_c0_program_add_block(&program, &block_id)) {
        minic_c0_program_destroy(&program);
        return fail("add block");
    }

    (void)memset(&local, 0, sizeof(local));
    local.type = byte_type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add byte scalar");
    }

    local.element_count = 3U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add byte array");
    }

    local.type = minic_type_int();
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add int scalar");
    }

    local.element_count = 3U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add int array");
    }

    if (!minic_type_pointer_to(minic_type_int(), &pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("construct pointer type");
    }
    local.type = pointer_type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add pointer scalar");
    }

    local.element_count = 2U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add pointer array");
    }

    local.type = minic_type_int();
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add trailing int");
    }

    local.type = record_type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add record local");
    }

    if (!minic_c0_program_add_function(
            &program,
            "sample",
            6U,
            0U,
            8U,
            block_id,
            &function_id) ||
        !minic_riscv64_layout_program(
            "layout-test",
            &program,
            &diagnostic)) {
        minic_c0_program_destroy(&program);
        return fail("layout program");
    }

    hooks_record = minic_c0_program_record(&program, hooks_record_id);
    if (hooks_record == NULL || hooks_record->storage_size != 16U ||
        hooks_record->alignment != 8U || hooks_record->fields[0].storage_offset != 0U ||
        hooks_record->fields[1].storage_offset != 8U) {
        minic_c0_program_destroy(&program);
        return fail("function pointer record field layout");
    }

    floating_record = minic_c0_program_record(&program, floating_record_id);
    if (floating_record == NULL || floating_record->storage_size != 24U ||
        floating_record->alignment != 8U || floating_record->fields[0].storage_offset != 0U ||
        floating_record->fields[1].storage_offset != 8U ||
        floating_record->fields[2].storage_offset != 16U) {
        minic_c0_program_destroy(&program);
        return fail("double record field layout");
    }

    record = minic_c0_program_record(&program, record_id);
    if (record == NULL || record->storage_size != 24U || record->alignment != 4U ||
        record->fields[0].storage_offset != 0U || record->fields[1].storage_offset != 4U ||
        record->fields[2].storage_offset != 20U) {
        minic_c0_program_destroy(&program);
        return fail("record field layout");
    }

    function = minic_c0_program_function(&program, function_id);
    if (function == NULL || function->local_storage_size != 76U ||
        program.locals[0].storage_offset != 0U || program.locals[1].storage_offset != 1U ||
        program.locals[2].storage_offset != 4U || program.locals[3].storage_offset != 8U ||
        program.locals[4].storage_offset != 24U || program.locals[5].storage_offset != 32U ||
        program.locals[6].storage_offset != 48U || program.locals[7].storage_offset != 52U) {
        minic_c0_program_destroy(&program);
        return fail("mixed byte, scalar, array, pointer, and record offsets");
    }

    program.locals[1].element_count = 0U;
    diagnostic.message[0] = '\0';
    if (minic_riscv64_layout_program(
            "layout-zero",
            &program,
            &diagnostic) ||
        strcmp(
            diagnostic.message,
            "local object size is invalid for the RV64 target") != 0) {
        minic_c0_program_destroy(&program);
        return fail("zero-element byte object accepted");
    }

    program.locals[1].element_count = 3U;
    program.locals[3].element_count = SIZE_MAX;
    diagnostic.message[0] = '\0';
    if (minic_riscv64_layout_program(
            "layout-overflow",
            &program,
            &diagnostic) ||
        strcmp(
            diagnostic.message,
            "local object size is invalid for the RV64 target") != 0) {
        minic_c0_program_destroy(&program);
        return fail("array size overflow accepted");
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS target/riscv64/layout\n");
    return 0;
}
