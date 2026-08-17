#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/ast.c"
text = path.read_text()

old = '''    if (minic_type_pointer_equality_compatible(left->type, right->type)) {
        return true;
    }
    return (minic_type_is_pointer(left->type) &&
            minic_c0_expression_is_null_pointer_constant_v0(program, right_expression_id)) ||
           (minic_c0_expression_is_null_pointer_constant_v0(program, left_expression_id) &&
            minic_type_is_pointer(right->type));
'''
new = '''    if (minic_type_pointer_equality_compatible(left->type, right->type) ||
        (minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
         minic_c0_gnu_void_function_pointer_assignment_compatible(left->type, right->type))) {
        return true;
    }
    return (minic_type_is_pointer(left->type) &&
            minic_c0_expression_is_null_pointer_constant_v0(program, right_expression_id)) ||
           (minic_c0_expression_is_null_pointer_constant_v0(program, left_expression_id) &&
            minic_type_is_pointer(right->type));
'''
if text.count(old) != 1:
    raise SystemExit(f"pointer equality body count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
