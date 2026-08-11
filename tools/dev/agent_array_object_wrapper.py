from pathlib import Path
import runpy

root = Path(__file__).resolve().parents[2]
generator = Path(__file__).with_name("agent_array_object_semantics_patch.py")
runpy.run_path(str(generator), run_name="__main__")

path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
old = '''static bool incomplete_array_is_extern_object_type(const MinicC0Program *program,
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
new = '''static bool type_owns_array_descriptor(MinicType type,
                                       MinicArrayTypeId array_type_id,
                                       bool require_pointer) {
    return type.base_kind == MINIC_TYPE_BASE_ARRAY && type.array_type_id == array_type_id &&
           (!require_pointer || type.pointer_depth != 0U);
}

static bool incomplete_array_has_semantic_owner(const MinicC0Program *program,
                                                MinicArrayTypeId array_type_id) {
    size_t index;

    if (program == NULL) {
        return false;
    }
    for (index = 0U; index < program->expression_count; ++index) {
        if (type_owns_array_descriptor(program->expressions[index].type, array_type_id, false)) {
            return true;
        }
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        if (type_owns_array_descriptor(program->type_aliases[index].type, array_type_id, false)) {
            return true;
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = &program->global_objects[index];
        if ((object->is_extern && minic_type_is_array(object->type) &&
             object->type.array_type_id == array_type_id) ||
            type_owns_array_descriptor(object->type, array_type_id, true)) {
            return true;
        }
    }
    for (index = 0U; index < program->local_count; ++index) {
        if (type_owns_array_descriptor(program->locals[index].type, array_type_id, true)) {
            return true;
        }
    }
    for (index = 0U; index < program->record_count; ++index) {
        const MinicRecord *record;
        size_t field_index;

        record = &program->records[index];
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            if (type_owns_array_descriptor(
                    record->fields[field_index].type, array_type_id, true)) {
                return true;
            }
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function;
        size_t parameter_index;

        function = &program->functions[index];
        if (type_owns_array_descriptor(function->return_type, array_type_id, true)) {
            return true;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
            if (type_owns_array_descriptor(
                    function->parameter_types[parameter_index], array_type_id, true)) {
                return true;
            }
        }
    }
    for (index = 0U; index < program->function_type_count; ++index) {
        const MinicFunctionType *function_type;
        size_t parameter_index;

        function_type = &program->function_types[index];
        if (type_owns_array_descriptor(function_type->return_type, array_type_id, true)) {
            return true;
        }
        for (parameter_index = 0U; parameter_index < function_type->parameter_count;
             ++parameter_index) {
            if (type_owns_array_descriptor(
                    function_type->parameter_types[parameter_index], array_type_id, true)) {
                return true;
            }
        }
    }
    return false;
}
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one incomplete-array extern owner helper, got {text.count(old)}")
text = text.replace(old, new, 1)
old_use = '''        if ((array_type->element_count == 0U &&
             !incomplete_array_is_extern_object_type(program, index)) ||
'''
new_use = '''        if ((array_type->element_count == 0U &&
             !incomplete_array_has_semantic_owner(program, index)) ||
'''
if text.count(old_use) != 1:
    raise SystemExit(f"expected one incomplete-array owner use, got {text.count(old_use)}")
text = text.replace(old_use, new_use, 1)
path.write_text(text)
print("PASS added incomplete-array semantic ownership")
