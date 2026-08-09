#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
old = '''    } else if (base->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
               base->value_category == MINIC_VALUE_LVALUE) {
        const MinicGlobalObject *object;
        const MinicArrayType *array_type;

        object = minic_c0_program_global_object(program, base->value.global_object_id);
        if (object == NULL || !minic_type_is_array(object->type)) {
            return false;
        }
        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        base_is_array_object = array_type != NULL;
        if (!base_is_array_object ||
            !minic_type_equal(array_type->element_type, expression->type)) {
            return false;
        }
'''
new = '''    } else if (base->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
               base->value_category == MINIC_VALUE_LVALUE) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, base->value.global_object_id);
        if (object == NULL) {
            return false;
        }
        if (minic_type_is_array(object->type)) {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, object->type.array_type_id);
            base_is_array_object = array_type != NULL;
            if (!base_is_array_object ||
                !minic_type_equal(array_type->element_type, expression->type)) {
                return false;
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected global-object subscript branch")
path.write_text(text.replace(old, new, 1))
print("staged global pointer subscript lowering")
