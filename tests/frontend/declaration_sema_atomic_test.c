#include "frontend/declaration_sema.h"

#include <assert.h>
#include <string.h>

static void test_type_conflict_does_not_commit_section(void) {
    MinicC0Program program;
    MinicDeclarationExternalObjectAttributes attributes;
    MinicDeclarationExternalObjectMergeStatus status;
    const MinicGlobalObject *object;
    MinicGlobalObjectId object_id;
    MinicType existing_type;
    MinicType declared_type;

    minic_c0_program_initialize(&program);
    assert(minic_c0_program_add_array_type(&program, minic_type_int(), 2U, &existing_type));
    assert(minic_c0_program_add_array_type(&program, minic_type_int(), 3U, &declared_type));
    assert(minic_c0_program_add_extern_global_object(
        &program, "typed", strlen("typed"), existing_type, false, &object_id));

    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.section_name = ".new";
    attributes.section_name_length = strlen(".new");
    attributes.has_section = true;
    attributes.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;

    status = minic_declaration_merge_external_object(
        &program, object_id, declared_type, &attributes);
    assert(status == MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_TYPE_CONFLICT);

    object = minic_c0_program_global_object(&program, object_id);
    assert(object != NULL);
    assert(object->section_name == NULL);
    assert(program.array_types[existing_type.array_type_id].element_count == 2U);

    minic_c0_program_destroy(&program);
}

static void test_identical_section_can_commit_array_completion(void) {
    MinicC0Program program;
    MinicDeclarationExternalObjectAttributes attributes;
    MinicDeclarationExternalObjectMergeStatus status;
    const MinicArrayType *array_type;
    const MinicGlobalObject *object;
    MinicGlobalObjectId object_id;
    MinicType existing_type;
    MinicType declared_type;

    minic_c0_program_initialize(&program);
    assert(minic_c0_program_add_incomplete_array_type(&program, minic_type_int(), &existing_type));
    assert(minic_c0_program_add_array_type(&program, minic_type_int(), 4U, &declared_type));
    assert(minic_c0_program_add_extern_global_object(
        &program, "same", strlen("same"), existing_type, false, &object_id));
    assert(minic_c0_global_object_set_section(
        &program, object_id, ".same", strlen(".same")));

    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.section_name = ".same";
    attributes.section_name_length = strlen(".same");
    attributes.has_section = true;
    attributes.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;

    status = minic_declaration_merge_external_object(
        &program, object_id, declared_type, &attributes);
    assert(status == MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_OK);

    object = minic_c0_program_global_object(&program, object_id);
    array_type = &program.array_types[existing_type.array_type_id];
    assert(object != NULL);
    assert(object->section_name_length == strlen(".same"));
    assert(memcmp(object->section_name, ".same", strlen(".same")) == 0);
    assert(array_type->element_count == 4U);
    assert(!array_type->is_zero_length);

    minic_c0_program_destroy(&program);
}

static void test_external_object_creation_commits_complete_entity(void) {
    MinicC0Program program;
    MinicDeclarationExternalObjectAttributes attributes;
    MinicDeclarationExternalObjectCreateStatus status;
    const MinicGlobalObject *object;
    MinicGlobalObjectId object_id;

    minic_c0_program_initialize(&program);
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.section_name = ".sema";
    attributes.section_name_length = strlen(".sema");
    attributes.explicit_alignment = 16U;
    attributes.visibility = MINIC_SYMBOL_VISIBILITY_HIDDEN;
    attributes.has_section = true;
    attributes.has_visibility = true;

    status = minic_declaration_create_external_object(&program,
                                                      "fresh",
                                                      strlen("fresh"),
                                                      minic_type_int(),
                                                      true,
                                                      true,
                                                      false,
                                                      &attributes,
                                                      &object_id);
    assert(status == MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK);
    assert(program.global_object_count == 1U);
    object = minic_c0_program_global_object(&program, object_id);
    assert(object != NULL);
    assert(object->is_extern);
    assert(object->is_read_only);
    assert(object->is_weak);
    assert(!object->is_block_scope_extern_only);
    assert(object->explicit_alignment == 16U);
    assert(object->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN);
    assert(object->section_name_length == strlen(".sema"));
    assert(memcmp(object->section_name, ".sema", strlen(".sema")) == 0);

    minic_c0_program_destroy(&program);
}

static void test_external_object_creation_rejects_invalid_facts_without_publish(void) {
    MinicC0Program program;
    MinicDeclarationExternalObjectAttributes attributes;
    MinicDeclarationExternalObjectCreateStatus status;
    MinicGlobalObjectId object_id;

    minic_c0_program_initialize(&program);
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.section_name = NULL;
    attributes.section_name_length = 4U;
    attributes.has_section = true;
    attributes.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;

    status = minic_declaration_create_external_object(&program,
                                                      "invalid",
                                                      strlen("invalid"),
                                                      minic_type_int(),
                                                      false,
                                                      false,
                                                      false,
                                                      &attributes,
                                                      &object_id);
    assert(status == MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_INVALID);
    assert(program.global_object_count == 0U);

    minic_c0_program_destroy(&program);
}

static void test_external_object_creation_conflict_does_not_publish_second_entity(void) {
    MinicC0Program program;
    MinicDeclarationExternalObjectAttributes attributes;
    MinicDeclarationExternalObjectCreateStatus status;
    MinicGlobalObjectId first_id;
    MinicGlobalObjectId second_id;

    minic_c0_program_initialize(&program);
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    status = minic_declaration_create_external_object(&program,
                                                      "duplicate",
                                                      strlen("duplicate"),
                                                      minic_type_int(),
                                                      false,
                                                      false,
                                                      true,
                                                      &attributes,
                                                      &first_id);
    assert(status == MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK);
    status = minic_declaration_create_external_object(&program,
                                                      "duplicate",
                                                      strlen("duplicate"),
                                                      minic_type_long(),
                                                      false,
                                                      false,
                                                      false,
                                                      &attributes,
                                                      &second_id);
    assert(status == MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_COMMIT_FAILED);
    assert(program.global_object_count == 1U);
    assert(program.global_objects[first_id].is_block_scope_extern_only);

    minic_c0_program_destroy(&program);
}

int main(void) {
    test_type_conflict_does_not_commit_section();
    test_identical_section_can_commit_array_completion();
    test_external_object_creation_commits_complete_entity();
    test_external_object_creation_rejects_invalid_facts_without_publish();
    test_external_object_creation_conflict_does_not_publish_second_entity();
    return 0;
}
