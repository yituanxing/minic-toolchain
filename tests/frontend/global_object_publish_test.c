#include "frontend/ast.h"

#include <assert.h>
#include <string.h>

static void test_publish_complete_extern_object(void) {
    MinicC0Program program;
    const MinicGlobalObject *object;
    MinicGlobalObjectId object_id;

    minic_c0_program_initialize(&program);
    assert(minic_c0_program_add_extern_global_object_with_metadata(&program,
                                                                   "published",
                                                                   9U,
                                                                   minic_type_int(),
                                                                   true,
                                                                   ".probe",
                                                                   6U,
                                                                   32U,
                                                                   MINIC_SYMBOL_VISIBILITY_HIDDEN,
                                                                   true,
                                                                   true,
                                                                   &object_id));
    assert(object_id == 0U);
    assert(program.global_object_count == 1U);
    object = minic_c0_program_global_object(&program, object_id);
    assert(object != NULL);
    assert(object->name_length == 9U);
    assert(memcmp(object->name, "published", 9U) == 0);
    assert(object->section_name_length == 6U);
    assert(memcmp(object->section_name, ".probe", 6U) == 0);
    assert(minic_type_equal(object->type, minic_type_int()));
    assert(object->explicit_alignment == 32U);
    assert(object->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN);
    assert(object->is_extern);
    assert(object->is_read_only);
    assert(object->is_weak);
    assert(object->is_block_scope_extern_only);
    minic_c0_program_destroy(&program);
}

static void test_invalid_metadata_does_not_publish(void) {
    MinicC0Program program;
    MinicGlobalObjectId object_id;

    minic_c0_program_initialize(&program);
    assert(!minic_c0_program_add_extern_global_object_with_metadata(&program,
                                                                    "bad-section",
                                                                    11U,
                                                                    minic_type_int(),
                                                                    false,
                                                                    NULL,
                                                                    4U,
                                                                    0U,
                                                                    MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                                                    false,
                                                                    false,
                                                                    &object_id));
    assert(program.global_object_count == 0U);
    assert(!minic_c0_program_add_extern_global_object_with_metadata(&program,
                                                                    "bad-align",
                                                                    9U,
                                                                    minic_type_int(),
                                                                    false,
                                                                    NULL,
                                                                    0U,
                                                                    3U,
                                                                    MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                                                    false,
                                                                    false,
                                                                    &object_id));
    assert(program.global_object_count == 0U);
    assert(!minic_c0_program_add_extern_global_object_with_metadata(
        &program,
        "bad-visibility",
        14U,
        minic_type_int(),
        false,
        NULL,
        0U,
        0U,
        (MinicSymbolVisibility)(MINIC_SYMBOL_VISIBILITY_PROTECTED + 1),
        false,
        false,
        &object_id));
    assert(program.global_object_count == 0U);
    minic_c0_program_destroy(&program);
}

static void test_name_conflict_does_not_publish_second_entity(void) {
    MinicC0Program program;
    MinicGlobalObjectId first_id;
    MinicGlobalObjectId second_id;

    minic_c0_program_initialize(&program);
    assert(minic_c0_program_add_extern_global_object_with_metadata(&program,
                                                                   "same",
                                                                   4U,
                                                                   minic_type_int(),
                                                                   false,
                                                                   NULL,
                                                                   0U,
                                                                   0U,
                                                                   MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                                                   false,
                                                                   false,
                                                                   &first_id));
    assert(first_id == 0U);
    assert(!minic_c0_program_add_extern_global_object_with_metadata(&program,
                                                                    "same",
                                                                    4U,
                                                                    minic_type_long(),
                                                                    false,
                                                                    ".other",
                                                                    6U,
                                                                    16U,
                                                                    MINIC_SYMBOL_VISIBILITY_HIDDEN,
                                                                    true,
                                                                    false,
                                                                    &second_id));
    assert(program.global_object_count == 1U);
    minic_c0_program_destroy(&program);
}

int main(void) {
    test_publish_complete_extern_object();
    test_invalid_metadata_does_not_publish();
    test_name_conflict_does_not_publish_second_entity();
    return 0;
}
