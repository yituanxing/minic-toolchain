#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/ast_verifier.c")
text = path.read_text()

marker = "bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form) {\n"
helper = r'''static bool incomplete_array_is_extern_object_type(const MinicC0Program *program,
                                                   MinicArrayTypeId array_type_id) {
    size_t object_index;

    if (program == NULL) {
        return false;
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object;

        object = &program->global_objects[object_index];
        if (object->is_extern && minic_type_is_array(object->type) &&
            object->type.array_type_id == array_type_id) {
            return true;
        }
    }
    return false;
}

'''
if text.count(marker) != 1:
    raise SystemExit("unexpected verifier entry marker")
text = text.replace(marker, helper + marker, 1)

old = '''        array_type = &program->array_types[index];
        if (array_type->element_count == 0U || !type_is_valid(program, array_type->element_type) ||
            minic_type_is_function(array_type->element_type)) {
            return false;
        }
'''
new = '''        array_type = &program->array_types[index];
        if ((array_type->element_count == 0U &&
             !incomplete_array_is_extern_object_type(program, index)) ||
            !type_is_valid(program, array_type->element_type) ||
            minic_type_is_function(array_type->element_type)) {
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected array descriptor verifier block")
path.write_text(text.replace(old, new, 1))
print("staged AST verifier support for extern incomplete arrays")
