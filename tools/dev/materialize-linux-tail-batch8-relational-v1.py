from pathlib import Path

path = Path("src/frontend/ast.c")
text = path.read_text()
old = '''    return program != NULL && minic_type_pointee(left, &left_pointee) &&
           minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           !minic_type_is_void(left_unqualified) && !minic_type_is_function(left_unqualified);
'''
new = '''    return program != NULL && minic_type_pointee(left, &left_pointee) &&
           minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_c0_types_compatible(program, left_unqualified, right_unqualified) &&
           !minic_type_is_void(left_unqualified) && !minic_type_is_function(left_unqualified);
'''
if text.count(old) != 1:
    raise SystemExit("expected one batch8 pointer relational replacement")
path.write_text(text.replace(old, new, 1))
