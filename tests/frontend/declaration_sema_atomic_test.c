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

int main(void) {
    test_type_conflict_does_not_commit_section();
    test_identical_section_can_commit_array_completion();
    return 0;
}
