from pathlib import Path

path = Path("src/frontend/ast.c")
text = path.read_text()
old = '''           minic_c0_types_compatible(program, left_unqualified, right_unqualified) &&
           !minic_type_is_void(left_unqualified) && !minic_type_is_function(left_unqualified);
'''
new = '''           minic_c0_types_compatible(program, left_unqualified, right_unqualified) &&
           !minic_type_is_function(left_unqualified);
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one relational compatibility tail, found {count}")
path.write_text(text.replace(old, new, 1))
