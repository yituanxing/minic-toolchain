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
    MinicLocal local;
    MinicType pointer_type;
    MinicDiagnostic diagnostic;
    const MinicFunction *function;

    minic_c0_program_initialize(&program);
    (void)memset(&diagnostic, 0, sizeof(diagnostic));

    if (!minic_c0_program_add_block(&program, &block_id)) {
        minic_c0_program_destroy(&program);
        return fail("add block");
    }

    (void)memset(&local, 0, sizeof(local));
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

    if (!minic_c0_program_add_function(
            &program,
            "sample",
            6U,
            0U,
            5U,
            block_id,
            &function_id) ||
        !minic_riscv64_layout_program(
            "layout-test",
            &program,
            &diagnostic)) {
        minic_c0_program_destroy(&program);
        return fail("layout program");
    }

    function = minic_c0_program_function(&program, function_id);
    if (function == NULL || function->local_storage_size != 44U ||
        program.locals[0].storage_offset != 0U ||
        program.locals[1].storage_offset != 4U ||
        program.locals[2].storage_offset != 16U ||
        program.locals[3].storage_offset != 24U ||
        program.locals[4].storage_offset != 40U) {
        minic_c0_program_destroy(&program);
        return fail("mixed scalar and array offsets");
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
        return fail("zero-element object accepted");
    }

    program.locals[1].element_count = SIZE_MAX;
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
