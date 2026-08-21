#include "frontend/ast.h"
#include "frontend/sema.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    (void)fprintf(stderr, "FAIL frontend/sema-declarator: %s\n", message);
    return 1;
}

int main(void) {
    MinicC0Program program;
    MinicArrayDeclaratorSyntax declarator;
    MinicType committed_type;
    MinicType incomplete_type;
    const MinicArrayType *outer;
    const MinicArrayType *inner;
    size_t count_before;
    size_t count_after_commit;
    size_t count_after_incomplete;

    minic_c0_program_initialize(&program);
    (void)memset(&declarator, 0, sizeof(declarator));
    declarator.bounds[0] = 3U;
    declarator.bounds[1] = 5U;
    declarator.dimension_count = 2U;

    count_before = program.array_type_count;
    if (!minic_sema_materialize_array_declarator(
            &program, minic_type_unsigned_char(), &declarator, &committed_type) ||
        program.array_type_count != count_before + 2U || !minic_type_is_array(committed_type)) {
        minic_c0_program_destroy(&program);
        return fail("semantic materialization did not commit exactly two array types");
    }

    outer = minic_c0_program_array_type(&program, committed_type.array_type_id);
    inner = outer != NULL && minic_type_is_array(outer->element_type)
                ? minic_c0_program_array_type(&program, outer->element_type.array_type_id)
                : NULL;
    if (outer == NULL || outer->element_count != 3U || outer->is_zero_length || inner == NULL ||
        inner->element_count != 5U || inner->is_zero_length ||
        !minic_type_equal(inner->element_type, minic_type_unsigned_char())) {
        minic_c0_program_destroy(&program);
        return fail("materialized array shape is incorrect");
    }

    count_after_commit = program.array_type_count;
    if (!minic_sema_array_declarator_compatible_with_type(
            &program, committed_type, minic_type_unsigned_char(), &declarator) ||
        program.array_type_count != count_after_commit) {
        minic_c0_program_destroy(&program);
        return fail("compatibility query mutated Program");
    }

    declarator.bounds[1] = 6U;
    if (minic_sema_array_declarator_compatible_with_type(
            &program, committed_type, minic_type_unsigned_char(), &declarator) ||
        program.array_type_count != count_after_commit) {
        minic_c0_program_destroy(&program);
        return fail("incompatible inner bound was accepted");
    }
    declarator.bounds[1] = 5U;

    declarator.outermost_incomplete = true;
    if (!minic_sema_array_declarator_compatible_with_type(
            &program, committed_type, minic_type_unsigned_char(), &declarator) ||
        program.array_type_count != count_after_commit) {
        minic_c0_program_destroy(&program);
        return fail("incomplete outer declaration did not match complete existing type");
    }
    declarator.outermost_incomplete = false;

    declarator.zero_length_mask = 1U << 1U;
    if (minic_sema_array_declarator_compatible_with_type(
            &program, committed_type, minic_type_unsigned_char(), &declarator) ||
        program.array_type_count != count_after_commit) {
        minic_c0_program_destroy(&program);
        return fail("zero-length inner declaration matched fixed existing type");
    }
    declarator.zero_length_mask = 0U;

    if (minic_sema_array_declarator_compatible_with_type(
            &program, committed_type, minic_type_int(), &declarator) ||
        program.array_type_count != count_after_commit) {
        minic_c0_program_destroy(&program);
        return fail("different element type was accepted");
    }

    if (!minic_c0_program_add_incomplete_array_type(
            &program, minic_type_unsigned_char(), &incomplete_type)) {
        minic_c0_program_destroy(&program);
        return fail("cannot build existing incomplete array fixture");
    }
    count_after_incomplete = program.array_type_count;
    (void)memset(&declarator, 0, sizeof(declarator));
    declarator.bounds[0] = 7U;
    declarator.dimension_count = 1U;
    if (!minic_sema_array_declarator_compatible_with_type(
            &program, incomplete_type, minic_type_unsigned_char(), &declarator) ||
        program.array_type_count != count_after_incomplete) {
        minic_c0_program_destroy(&program);
        return fail("complete redeclaration did not match existing incomplete array");
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS frontend/sema-declarator\n");
    return 0;
}
