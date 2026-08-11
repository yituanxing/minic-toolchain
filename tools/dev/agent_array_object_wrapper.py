from pathlib import Path
import re
import runpy

root = Path(__file__).resolve().parents[2]
generator = Path(__file__).with_name("agent_array_object_semantics_patch.py")
runpy.run_path(str(generator), run_name="__main__")

# A legacy local/member may itself carry an already-materialized inner ArrayType.
# Mark whether expression->type is the whole array type or only the element/inner type.
path = root / "src/frontend/ast.h"
text = path.read_text()
old = "    bool is_incomplete;\n    bool is_zero_length;\n} MinicArrayObjectInfo;"
new = "    bool is_incomplete;\n    bool is_zero_length;\n    bool has_materialized_type;\n} MinicArrayObjectInfo;"
if text.count(old) != 1:
    raise SystemExit(f"expected one ArrayObjectInfo tail, got {text.count(old)}")
path.write_text(text.replace(old, new, 1))

path = root / "src/frontend/ast.c"
text = path.read_text()
pattern = r"bool minic_c0_expression_array_object_info\(const MinicC0Program \*program,.*?\n}\n"
replacement = '''bool minic_c0_expression_array_object_info(const MinicC0Program *program,
                                           const MinicExpression *expression,
                                           MinicArrayObjectInfo *info) {
    MinicArrayObjectInfo resolved;

    if (program == NULL || expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    (void)memset(&resolved, 0, sizeof(resolved));
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(program, expression->value.local_id);
        if (local != NULL && local->is_array) {
            resolved.element_type = expression->type;
            resolved.element_count = local->element_count;
        } else if (!minic_type_is_array(expression->type)) {
            return false;
        } else {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, expression->type.array_type_id);
            if (array_type == NULL) {
                return false;
            }
            resolved.element_type = array_type->element_type;
            resolved.element_count = array_type->element_count;
            resolved.is_incomplete = array_type->element_count == 0U;
            resolved.has_materialized_type = true;
        }
    } else if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field != NULL && field->is_array) {
            resolved.element_type = expression->type;
            resolved.element_count = field->element_count;
            resolved.is_incomplete = field->is_flexible_array;
            resolved.is_zero_length = field->is_zero_length_array;
        } else if (!minic_type_is_array(expression->type)) {
            return false;
        } else {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, expression->type.array_type_id);
            if (array_type == NULL) {
                return false;
            }
            resolved.element_type = array_type->element_type;
            resolved.element_count = array_type->element_count;
            resolved.is_incomplete = array_type->element_count == 0U;
            resolved.has_materialized_type = true;
        }
    } else if (minic_type_is_array(expression->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, expression->type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        resolved.element_type = array_type->element_type;
        resolved.element_count = array_type->element_count;
        resolved.is_incomplete = array_type->element_count == 0U;
        resolved.has_materialized_type = true;
    } else {
        return false;
    }
    if (info != NULL) {
        *info = resolved;
    }
    return true;
}
'''
text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
if count != 1:
    raise SystemExit(f"expected one generated array-object query, got {count}")
path.write_text(text)

path = root / "src/frontend/parser_postfix.c"
text = path.read_text()
old = '''    if (minic_type_is_array(expression->type)) {
        *array_type = expression->type;
        return true;
    }
'''
new = '''    if (info.has_materialized_type) {
        *array_type = expression->type;
        return true;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one materialized-array fast path, got {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Incomplete ArrayType descriptors remain valid only with a real semantic owner.
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
            if (type_owns_array_descriptor(record->fields[field_index].type, array_type_id, true)) {
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
            if (type_owns_array_descriptor(function->parameter_types[parameter_index],
                                           array_type_id,
                                           true)) {
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
            if (type_owns_array_descriptor(function_type->parameter_types[parameter_index],
                                           array_type_id,
                                           true)) {
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
path.write_text(text.replace(old_use, new_use, 1))
print("PASS added multidimensional and incomplete-array semantic ownership")
