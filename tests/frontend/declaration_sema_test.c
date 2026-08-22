#include "frontend/declaration_sema.h"

#include <assert.h>
#include <string.h>

static MinicType add_fixed_array(MinicC0Program *program, MinicType element_type, size_t count) {
    MinicType type;

    assert(minic_c0_program_add_array_type(program, element_type, count, &type));
    return type;
}

static MinicType add_incomplete_array(MinicC0Program *program, MinicType element_type) {
    MinicType type;

    assert(minic_c0_program_add_incomplete_array_type(program, element_type, &type));
    return type;
}

static MinicType add_zero_length_array(MinicC0Program *program, MinicType element_type) {
    MinicType type;

    assert(minic_c0_program_add_zero_length_array_type(program, element_type, &type));
    return type;
}

static void test_fixed_multidimensional_array(void) {
    MinicC0Program program;
    MinicDeclarationArraySuffix suffix;
    MinicDeclarationArrayMaterializeStatus status;
    const MinicArrayType *outer;
    const MinicArrayType *inner;
    MinicType type;

    minic_c0_program_initialize(&program);
    (void)memset(&suffix, 0, sizeof(suffix));
    suffix.bounds[0] = 3U;
    suffix.bounds[1] = 4U;
    suffix.dimension_count = 2U;

    status = minic_declaration_materialize_array_suffix(
        &program, minic_type_int(), &suffix, &type);
    assert(status == MINIC_DECLARATION_ARRAY_MATERIALIZE_OK);
    assert(minic_type_is_array(type));
    assert(program.array_type_count == 2U);

    outer = &program.array_types[type.array_type_id];
    assert(outer->element_count == 3U);
    assert(!outer->is_zero_length);
    assert(minic_type_is_array(outer->element_type));
    inner = &program.array_types[outer->element_type.array_type_id];
    assert(inner->element_count == 4U);
    assert(!inner->is_zero_length);
    assert(minic_type_equal(inner->element_type, minic_type_int()));

    minic_c0_program_destroy(&program);
}

static void test_incomplete_and_zero_length_arrays(void) {
    MinicC0Program program;
    MinicDeclarationArraySuffix suffix;
    MinicDeclarationArrayMaterializeStatus status;
    const MinicArrayType *array_type;
    MinicType type;

    minic_c0_program_initialize(&program);
    (void)memset(&suffix, 0, sizeof(suffix));
    suffix.dimension_count = 1U;
    suffix.outermost_incomplete = true;
    status = minic_declaration_materialize_array_suffix(
        &program, minic_type_unsigned_char(), &suffix, &type);
    assert(status == MINIC_DECLARATION_ARRAY_MATERIALIZE_OK);
    array_type = &program.array_types[type.array_type_id];
    assert(array_type->element_count == 0U);
    assert(!array_type->is_zero_length);

    (void)memset(&suffix, 0, sizeof(suffix));
    suffix.dimension_count = 1U;
    suffix.zero_length_mask = 1U;
    status = minic_declaration_materialize_array_suffix(
        &program, minic_type_int(), &suffix, &type);
    assert(status == MINIC_DECLARATION_ARRAY_MATERIALIZE_OK);
    array_type = &program.array_types[type.array_type_id];
    assert(array_type->element_count == 0U);
    assert(array_type->is_zero_length);

    minic_c0_program_destroy(&program);
}

static void test_array_of_function_pointer(void) {
    MinicC0Program program;
    MinicDeclarationArraySuffix suffix;
    const MinicArrayType *array_type;
    const MinicFunctionType *function_type;
    MinicType parameter_types[1];
    MinicType pointee;
    MinicType type;

    minic_c0_program_initialize(&program);
    (void)memset(&suffix, 0, sizeof(suffix));
    suffix.bounds[0] = 4U;
    suffix.dimension_count = 1U;
    parameter_types[0] = minic_type_long();

    assert(minic_declaration_build_function_type(&program,
                                                 minic_type_int(),
                                                 parameter_types,
                                                 1U,
                                                 false,
                                                 1U,
                                                 1U,
                                                 0U,
                                                 &suffix,
                                                 &type));
    assert(program.function_type_count == 1U);
    assert(program.array_type_count == 1U);
    assert(minic_type_is_array(type));

    array_type = &program.array_types[type.array_type_id];
    assert(array_type->element_count == 4U);
    assert(minic_type_is_pointer(array_type->element_type));
    assert(minic_type_is_const(array_type->element_type));
    assert(minic_type_pointee(array_type->element_type, &pointee));
    assert(minic_type_is_function(pointee));

    function_type = &program.function_types[pointee.function_type_id];
    assert(minic_type_equal(function_type->return_type, minic_type_int()));
    assert(function_type->parameter_count == 1U);
    assert(minic_type_equal(function_type->parameter_types[0], minic_type_long()));
    assert(!function_type->is_variadic);

    minic_c0_program_destroy(&program);
}

static void test_invalid_transient_facts_do_not_commit(void) {
    MinicC0Program program;
    MinicDeclarationArraySuffix suffix;
    MinicDeclarationArrayMaterializeStatus status;
    MinicType type;

    minic_c0_program_initialize(&program);
    (void)memset(&suffix, 0, sizeof(suffix));
    suffix.dimension_count = MINIC_DECLARATION_MAX_ARRAY_DIMENSIONS + 1U;
    status = minic_declaration_materialize_array_suffix(
        &program, minic_type_int(), &suffix, &type);
    assert(status == MINIC_DECLARATION_ARRAY_MATERIALIZE_INVALID);
    assert(program.array_type_count == 0U);
    assert(program.function_type_count == 0U);

    (void)memset(&suffix, 0, sizeof(suffix));
    suffix.dimension_count = 1U;
    suffix.bounds[0] = 2U;
    assert(!minic_declaration_build_function_type(&program,
                                                  minic_type_int(),
                                                  NULL,
                                                  0U,
                                                  false,
                                                  1U,
                                                  2U,
                                                  0U,
                                                  &suffix,
                                                  &type));
    assert(program.array_type_count == 0U);
    assert(program.function_type_count == 0U);

    minic_c0_program_destroy(&program);
}

static void test_external_scalar_compatibility(void) {
    MinicC0Program program;

    minic_c0_program_initialize(&program);
    assert(minic_declaration_external_object_types_compatible(
        &program, minic_type_int(), minic_type_int()));
    assert(!minic_declaration_external_object_types_compatible(
        &program, minic_type_int(), minic_type_long()));
    assert(minic_declaration_merge_external_array_composite_type(
        &program, minic_type_int(), minic_type_int()));
    assert(!minic_declaration_merge_external_array_composite_type(
        &program, minic_type_int(), minic_type_long()));
    minic_c0_program_destroy(&program);
}

static void test_external_incomplete_array_completes(void) {
    MinicC0Program program;
    const MinicArrayType *existing_array;
    MinicType existing_type;
    MinicType declared_type;

    minic_c0_program_initialize(&program);
    existing_type = add_incomplete_array(&program, minic_type_int());
    declared_type = add_fixed_array(&program, minic_type_int(), 3U);

    assert(minic_declaration_external_object_types_compatible(
        &program, existing_type, declared_type));
    assert(minic_declaration_merge_external_array_composite_type(
        &program, existing_type, declared_type));
    assert(program.array_type_count == 2U);
    existing_array = &program.array_types[existing_type.array_type_id];
    assert(existing_array->element_count == 3U);
    assert(!existing_array->is_zero_length);
    assert(minic_type_equal(existing_array->element_type, minic_type_int()));

    minic_c0_program_destroy(&program);
}

static void test_external_incomplete_array_accepts_zero_length(void) {
    MinicC0Program program;
    const MinicArrayType *existing_array;
    MinicType existing_type;
    MinicType declared_type;
    MinicType fixed_type;

    minic_c0_program_initialize(&program);
    existing_type = add_incomplete_array(&program, minic_type_int());
    declared_type = add_zero_length_array(&program, minic_type_int());

    assert(minic_declaration_external_object_types_compatible(
        &program, existing_type, declared_type));
    assert(minic_declaration_merge_external_array_composite_type(
        &program, existing_type, declared_type));
    existing_array = &program.array_types[existing_type.array_type_id];
    assert(existing_array->element_count == 0U);
    assert(existing_array->is_zero_length);

    fixed_type = add_fixed_array(&program, minic_type_int(), 1U);
    assert(!minic_declaration_external_object_types_compatible(
        &program, existing_type, fixed_type));
    assert(!minic_declaration_merge_external_array_composite_type(
        &program, existing_type, fixed_type));
    existing_array = &program.array_types[existing_type.array_type_id];
    assert(existing_array->element_count == 0U);
    assert(existing_array->is_zero_length);

    minic_c0_program_destroy(&program);
}

static void test_external_fixed_array_conflict_is_non_mutating(void) {
    MinicC0Program program;
    const MinicArrayType *existing_array;
    MinicType existing_type;
    MinicType declared_type;

    minic_c0_program_initialize(&program);
    existing_type = add_fixed_array(&program, minic_type_int(), 2U);
    declared_type = add_fixed_array(&program, minic_type_int(), 3U);

    assert(!minic_declaration_external_object_types_compatible(
        &program, existing_type, declared_type));
    assert(!minic_declaration_merge_external_array_composite_type(
        &program, existing_type, declared_type));
    existing_array = &program.array_types[existing_type.array_type_id];
    assert(existing_array->element_count == 2U);
    assert(!existing_array->is_zero_length);
    assert(program.array_type_count == 2U);

    minic_c0_program_destroy(&program);
}

static void test_external_multidimensional_array_composition(void) {
    MinicC0Program program;
    const MinicArrayType *existing_outer;
    const MinicArrayType *existing_inner;
    MinicType existing_inner_type;
    MinicType existing_outer_type;
    MinicType declared_inner_type;
    MinicType declared_outer_type;

    minic_c0_program_initialize(&program);
    existing_inner_type = add_fixed_array(&program, minic_type_int(), 4U);
    existing_outer_type = add_incomplete_array(&program, existing_inner_type);
    declared_inner_type = add_fixed_array(&program, minic_type_int(), 4U);
    declared_outer_type = add_fixed_array(&program, declared_inner_type, 3U);

    assert(minic_declaration_external_object_types_compatible(
        &program, existing_outer_type, declared_outer_type));
    assert(minic_declaration_merge_external_array_composite_type(
        &program, existing_outer_type, declared_outer_type));
    existing_outer = &program.array_types[existing_outer_type.array_type_id];
    assert(existing_outer->element_count == 3U);
    assert(!existing_outer->is_zero_length);
    assert(minic_type_equal(existing_outer->element_type, existing_inner_type));
    existing_inner = &program.array_types[existing_inner_type.array_type_id];
    assert(existing_inner->element_count == 4U);
    assert(!existing_inner->is_zero_length);

    minic_c0_program_destroy(&program);
}

static void test_external_nested_conflict_does_not_partially_complete(void) {
    MinicC0Program program;
    const MinicArrayType *existing_outer;
    const MinicArrayType *existing_inner;
    MinicType existing_inner_type;
    MinicType existing_outer_type;
    MinicType declared_inner_type;
    MinicType declared_outer_type;

    minic_c0_program_initialize(&program);
    existing_inner_type = add_incomplete_array(&program, minic_type_int());
    existing_outer_type = add_fixed_array(&program, existing_inner_type, 2U);
    declared_inner_type = add_fixed_array(&program, minic_type_int(), 4U);
    declared_outer_type = add_fixed_array(&program, declared_inner_type, 3U);

    assert(!minic_declaration_external_object_types_compatible(
        &program, existing_outer_type, declared_outer_type));
    assert(!minic_declaration_merge_external_array_composite_type(
        &program, existing_outer_type, declared_outer_type));
    existing_outer = &program.array_types[existing_outer_type.array_type_id];
    existing_inner = &program.array_types[existing_inner_type.array_type_id];
    assert(existing_outer->element_count == 2U);
    assert(!existing_outer->is_zero_length);
    assert(existing_inner->element_count == 0U);
    assert(!existing_inner->is_zero_length);

    minic_c0_program_destroy(&program);
}

int main(void) {
    test_fixed_multidimensional_array();
    test_incomplete_and_zero_length_arrays();
    test_array_of_function_pointer();
    test_invalid_transient_facts_do_not_commit();
    test_external_scalar_compatibility();
    test_external_incomplete_array_completes();
    test_external_incomplete_array_accepts_zero_length();
    test_external_fixed_array_conflict_is_non_mutating();
    test_external_multidimensional_array_composition();
    test_external_nested_conflict_does_not_partially_complete();
    return 0;
}
