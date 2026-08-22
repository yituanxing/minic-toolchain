from pathlib import Path


ast_h = Path("src/frontend/ast.h")
text = ast_h.read_text()
anchor = '''bool minic_c0_program_add_extern_global_object(MinicC0Program *program,
                                               const char *name,
                                               size_t name_length,
                                               MinicType type,
                                               bool is_read_only,
                                               MinicGlobalObjectId *global_object_id);
'''
addition = anchor + '''bool minic_c0_program_add_extern_global_object_with_metadata(
    MinicC0Program *program,
    const char *name,
    size_t name_length,
    MinicType type,
    bool is_read_only,
    const char *section_name,
    size_t section_name_length,
    size_t explicit_alignment,
    MinicSymbolVisibility visibility,
    bool is_weak,
    bool is_block_scope_extern_only,
    MinicGlobalObjectId *global_object_id);
'''
if text.count(anchor) != 1:
    raise SystemExit("ast.h extern add declaration anchor is not unique")
text = text.replace(anchor, addition, 1)
ast_h.write_text(text)

ast_global = Path("src/frontend/ast_global.c")
text = ast_global.read_text()
start_marker = "static bool add_global_object_entity("
end_marker = "bool minic_c0_program_add_tentative_global_object("
if text.count(start_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit("ast_global object creation anchors are not unique")
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = r'''typedef struct MinicGlobalObjectInitialState {
    const char *section_name;
    size_t section_name_length;
    size_t explicit_alignment;
    MinicSymbolVisibility visibility;
    bool is_internal;
    bool is_read_only;
    bool is_extern;
    bool is_weak;
    bool is_block_scope_extern_only;
} MinicGlobalObjectInitialState;

static bool global_object_initial_state_valid(const MinicGlobalObjectInitialState *state) {
    size_t alignment;

    if (state == NULL ||
        ((state->section_name == NULL) != (state->section_name_length == 0U)) ||
        state->section_name_length == SIZE_MAX ||
        state->visibility < MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        state->visibility > MINIC_SYMBOL_VISIBILITY_PROTECTED ||
        (state->is_weak && state->is_internal)) {
        return false;
    }
    alignment = state->explicit_alignment;
    return alignment == 0U || (alignment & (alignment - 1U)) == 0U;
}

static bool add_global_object_entity_with_state(MinicC0Program *program,
                                                const char *name,
                                                size_t name_length,
                                                MinicType type,
                                                const MinicGlobalObjectInitialState *state,
                                                MinicGlobalObjectId *global_object_id) {
    MinicGlobalObject object;

    if (program == NULL || name == NULL || global_object_id == NULL ||
        !global_object_initial_state_valid(state) ||
        (minic_type_is_void(type) && !state->is_extern) ||
        name_conflicts(program, name, name_length)) {
        return false;
    }

    (void)memset(&object, 0, sizeof(object));
    object.name = copy_name(name, name_length);
    if (object.name == NULL) {
        return false;
    }
    if (state->section_name != NULL) {
        object.section_name = copy_name(state->section_name, state->section_name_length);
        if (object.section_name == NULL) {
            free(object.name);
            return false;
        }
    }
    if (!grow_array((void **)&program->global_objects,
                    &program->global_object_capacity,
                    program->global_object_count,
                    sizeof(*program->global_objects))) {
        free(object.section_name);
        free(object.name);
        return false;
    }

    object.name_length = name_length;
    object.section_name_length = state->section_name_length;
    object.type = type;
    object.explicit_alignment = state->explicit_alignment;
    object.visibility = state->visibility;
    object.is_internal = state->is_internal;
    object.is_weak = state->is_weak;
    object.is_read_only = state->is_read_only;
    object.is_extern = state->is_extern;
    object.is_block_scope_extern_only = state->is_block_scope_extern_only;
    *global_object_id = program->global_object_count;
    program->global_objects[program->global_object_count] = object;
    program->global_object_count += 1U;
    return true;
}

static bool add_global_object_entity(MinicC0Program *program,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     bool is_internal,
                                     bool is_read_only,
                                     bool is_extern,
                                     MinicGlobalObjectId *global_object_id) {
    MinicGlobalObjectInitialState state;

    (void)memset(&state, 0, sizeof(state));
    state.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    state.is_internal = is_internal;
    state.is_read_only = is_read_only;
    state.is_extern = is_extern;
    return add_global_object_entity_with_state(
        program, name, name_length, type, &state, global_object_id);
}

bool minic_c0_program_add_global_object(MinicC0Program *program,
                                        const char *name,
                                        size_t name_length,
                                        MinicType type,
                                        bool is_internal,
                                        bool is_read_only,
                                        MinicGlobalObjectId *global_object_id) {
    return add_global_object_entity(
        program, name, name_length, type, is_internal, is_read_only, false, global_object_id);
}

bool minic_c0_program_add_extern_global_object(MinicC0Program *program,
                                               const char *name,
                                               size_t name_length,
                                               MinicType type,
                                               bool is_read_only,
                                               MinicGlobalObjectId *global_object_id) {
    return add_global_object_entity(
        program, name, name_length, type, false, is_read_only, true, global_object_id);
}

bool minic_c0_program_add_extern_global_object_with_metadata(
    MinicC0Program *program,
    const char *name,
    size_t name_length,
    MinicType type,
    bool is_read_only,
    const char *section_name,
    size_t section_name_length,
    size_t explicit_alignment,
    MinicSymbolVisibility visibility,
    bool is_weak,
    bool is_block_scope_extern_only,
    MinicGlobalObjectId *global_object_id) {
    MinicGlobalObjectInitialState state;

    (void)memset(&state, 0, sizeof(state));
    state.section_name = section_name;
    state.section_name_length = section_name_length;
    state.explicit_alignment = explicit_alignment;
    state.visibility = visibility;
    state.is_read_only = is_read_only;
    state.is_extern = true;
    state.is_weak = is_weak;
    state.is_block_scope_extern_only = is_block_scope_extern_only;
    return add_global_object_entity_with_state(
        program, name, name_length, type, &state, global_object_id);
}

'''
text = text[:start] + replacement + text[end:]
ast_global.write_text(text)

test = Path("tests/frontend/global_object_publish_test.c")
if test.exists():
    raise SystemExit("global_object_publish_test.c already exists")
test.write_text(r'''#include "frontend/ast.h"

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
                                                                   minic_type_const_int(),
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
    assert(minic_type_equal(object->type, minic_type_const_int()));
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
''')

workflow = Path(".github/workflows/frontend-ownership-contracts.yml")
text = workflow.read_text()
path_anchor = "      - 'tests/frontend/function_body_view_test.c'\n"
if text.count(path_anchor) != 1:
    raise SystemExit("ownership test path anchor is not unique")
text = text.replace(
    path_anchor,
    path_anchor + "      - 'tests/frontend/global_object_publish_test.c'\n",
    1,
)
step_anchor = "      - name: Build declaration semantic contract\n"
step = '''      - name: Build atomic global object publish contract
        shell: bash
        run: |
          set -Eeuo pipefail
          cc -std=c11 \\
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow \\
            -Wstrict-prototypes -Wmissing-prototypes -Werror \\
            -Iinclude -Isrc \\
            src/frontend/ast.c \\
            src/frontend/ast_global.c \\
            src/frontend/type.c \\
            tests/frontend/global_object_publish_test.c \\
            -o build/frontend-ownership/global-object-publish-test

      - name: Run atomic global object publish contract
        run: build/frontend-ownership/global-object-publish-test

'''
if text.count(step_anchor) != 1:
    raise SystemExit("ownership declaration step anchor is not unique")
text = text.replace(step_anchor, step + step_anchor, 1)
workflow.write_text(text)
