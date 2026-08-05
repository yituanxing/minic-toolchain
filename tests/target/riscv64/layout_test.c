#include "frontend/ast.h"
#include "frontend/type.h"
#include "target/riscv64/layout.h"

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
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add first int");
    }
    if (!minic_type_pointer_to(minic_type_int(), &pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("construct pointer type");
    }
    local.type = pointer_type;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add pointer");
    }
    local.type = minic_type_int();
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add second int");
    }

    if (!minic_c0_program_add_function(
            &program,
            "sample",
            6U,
            0U,
            3U,
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
    if (function == NULL || function->local_storage_size != 20U ||
        program.locals[0].storage_offset != 0U ||
        program.locals[1].storage_offset != 8U ||
        program.locals[2].storage_offset != 16U) {
        minic_c0_program_destroy(&program);
        return fail("mixed-width offsets");
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS target/riscv64/layout\n");
    return 0;
}
