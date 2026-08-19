#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/ast.c")
text = path.read_text()
old = '''    if (minic_type_is_pointer(target_type) && minic_type_is_pointer(source->type) &&
        (minic_c0_types_compatible(program, target_type, source->type) ||
         minic_c0_gnu_void_function_pointer_assignment_compatible(target_type, source->type))) {
        return true;
    }
'''
new = '''    if (minic_type_is_pointer(target_type) && minic_type_is_pointer(source->type) &&
        (minic_c0_types_compatible(program, target_type, source->type) ||
         minic_c0_gnu_void_function_pointer_assignment_compatible(target_type, source->type) ||
         minic_type_gnu_pointer_sign_compatible(target_type, source->type))) {
        return true;
    }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected assignment compatibility owner shape")
path.write_text(text.replace(old, new, 1))
print("materialized GNU same-rank pointer-sign assignment compatibility")
