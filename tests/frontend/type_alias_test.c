#include "frontend/ast.h"
#include "frontend/sema.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "FAIL frontend/type-alias: %s\n", message);
    return 1;
}

int main(void)
{
    MinicC0Program program;
    MinicType inner_array;
    MinicType outer_array;
    MinicType state_pointer;
    MinicTypeAliasId alias_id;
    MinicTypeAliasId duplicate_id;
    const MinicArrayType *descriptor;
    const MinicTypeAlias *alias;
    char alias_name[] = "state_t";

    minic_c0_program_initialize(&program);
    if (!minic_c0_program_add_array_type(
            &program,
            minic_type_int(),
            4U,
            &inner_array) ||
        !minic_c0_program_add_array_type(
            &program,
            inner_array,
            4U,
            &outer_array) ||
        !minic_c0_program_add_type_alias(
            &program,
            alias_name,
            strlen(alias_name),
            outer_array,
            &alias_id)) {
        minic_c0_program_destroy(&program);
        return fail("state_t construction");
    }
    alias_name[0] = 'X';

    alias = minic_c0_program_type_alias(&program, alias_id);
    if (alias == NULL || strcmp(alias->name, "state_t") != 0 ||
        !minic_type_equal(alias->type, outer_array) ||
        !minic_type_is_array(alias->type)) {
        minic_c0_program_destroy(&program);
        return fail("alias ownership and identity");
    }

    descriptor = minic_c0_program_array_type(
        &program,
        outer_array.array_type_id);
    if (descriptor == NULL || descriptor->element_count != 4U ||
        !minic_type_equal(descriptor->element_type, inner_array)) {
        minic_c0_program_destroy(&program);
        return fail("outer array descriptor");
    }
    descriptor = minic_c0_program_array_type(
        &program,
        inner_array.array_type_id);
    if (descriptor == NULL || descriptor->element_count != 4U ||
        !minic_type_is_integer(descriptor->element_type)) {
        minic_c0_program_destroy(&program);
        return fail("inner array descriptor");
    }

    if (!minic_type_pointer_to(alias->type, &state_pointer) ||
        !minic_type_is_pointer(state_pointer) ||
        !minic_type_pointee(state_pointer, &outer_array) ||
        !minic_type_equal(outer_array, alias->type)) {
        minic_c0_program_destroy(&program);
        return fail("pointer to multidimensional array");
    }

    {
        MinicArrayDeclaratorSyntax declarator;
        MinicType committed_type;
        const MinicArrayType *committed_outer;
        const MinicArrayType *committed_inner;
        size_t array_type_count_before;

        (void)memset(&declarator, 0, sizeof(declarator));
        declarator.bounds[0] = 3U;
        declarator.bounds[1] = 5U;
        declarator.dimension_count = 2U;
        array_type_count_before = program.array_type_count;

        if (program.array_type_count != array_type_count_before ||
            !minic_sema_materialize_array_declarator(
                &program, minic_type_unsigned_char(), &declarator, &committed_type) ||
            program.array_type_count != array_type_count_before + 2U ||
            !minic_type_is_array(committed_type)) {
            minic_c0_program_destroy(&program);
            return fail("array declarator semantic commit boundary");
        }
        committed_outer =
            minic_c0_program_array_type(&program, committed_type.array_type_id);
        committed_inner = committed_outer != NULL && minic_type_is_array(committed_outer->element_type)
                              ? minic_c0_program_array_type(
                                    &program, committed_outer->element_type.array_type_id)
                              : NULL;
        if (committed_outer == NULL || committed_outer->element_count != 3U ||
            committed_inner == NULL || committed_inner->element_count != 5U ||
            !minic_type_equal(committed_inner->element_type, minic_type_unsigned_char())) {
            minic_c0_program_destroy(&program);
            return fail("array declarator semantic materialization");
        }
    }

    if (minic_c0_program_add_type_alias(
            &program,
            "state_t",
            7U,
            minic_type_int(),
            &duplicate_id) ||
        minic_c0_program_add_array_type(
            &program,
            minic_type_void(),
            4U,
            &outer_array) ||
        minic_c0_program_add_array_type(
            &program,
            minic_type_int(),
            0U,
            &outer_array) ||
        minic_c0_program_type_alias(
            &program,
            MINIC_TYPE_ALIAS_INVALID) != NULL ||
        minic_c0_program_array_type(
            &program,
            MINIC_ARRAY_TYPE_INVALID) != NULL) {
        minic_c0_program_destroy(&program);
        return fail("invalid alias or array accepted");
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS frontend/type-alias\n");
    return 0;
}
