#!/usr/bin/env python3
from pathlib import Path

parser_path = Path("src/frontend/parser_function.c")
test_path = Path("tests/compiler/c0/declaration_head_variadic.c")

text = parser_path.read_text()
old = """        size_t typed_parameter_count;\n        size_t parameter_index;\n        bool declaration_is_weak;\n"""
new = """        size_t typed_parameter_count;\n        size_t parameter_index;\n        bool typed_is_variadic;\n        bool declaration_is_weak;\n"""
if text.count(old) != 1:
    raise SystemExit("unexpected function-typed local declaration shape")
text = text.replace(old, new, 1)

old = """        typed_return_type = function_type->return_type;\n        typed_parameter_count = function_type->parameter_count;\n"""
new = """        typed_return_type = function_type->return_type;\n        typed_parameter_count = function_type->parameter_count;\n        typed_is_variadic = function_type->is_variadic;\n"""
if text.count(old) != 1:
    raise SystemExit("unexpected function-type snapshot shape")
text = text.replace(old, new, 1)

old = """                                                    typed_parameter_count,\n                                                    false,\n                                                    is_internal,\n"""
new = """                                                    typed_parameter_count,\n                                                    typed_is_variadic,\n                                                    is_internal,\n"""
if text.count(old) != 1:
    raise SystemExit("unexpected function-typed entity recording shape")
text = text.replace(old, new, 1)
parser_path.write_text(text)

text = test_path.read_text()
anchor = """static int pick_first(int fixed, ...) {\n    return fixed;\n}\n\n"""
insert = """static int pick_first(int fixed, ...) {\n    return fixed;\n}\n\nint variadic_redecl(int fixed, ...);\nextern typeof(variadic_redecl) variadic_redecl;\n\n"""
if "extern typeof(variadic_redecl) variadic_redecl;" not in text:
    if text.count(anchor) != 1:
        raise SystemExit("unexpected focused test shape")
    text = text.replace(anchor, insert, 1)
test_path.write_text(text)

print("materialized function-typed variadic redeclaration fix")
