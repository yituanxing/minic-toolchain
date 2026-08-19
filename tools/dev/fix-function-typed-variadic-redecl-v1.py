#!/usr/bin/env python3
from pathlib import Path

parser_path = Path("src/frontend/parser_function.c")
verifier_path = Path("src/frontend/ast_verifier.c")
test_path = Path("tests/compiler/c0/declaration_head_variadic.c")

text = parser_path.read_text()
old = """        size_t typed_parameter_count;\n        size_t parameter_index;\n        bool declaration_is_weak;\n"""
new = """        size_t typed_parameter_count;\n        size_t parameter_index;\n        bool typed_is_variadic;\n        bool declaration_is_weak;\n"""
if old in text:
    if text.count(old) != 1:
        raise SystemExit("unexpected function-typed local declaration shape")
    text = text.replace(old, new, 1)
elif "bool typed_is_variadic;" not in text:
    raise SystemExit("function-typed variadic snapshot declaration missing")

old = """        typed_return_type = function_type->return_type;\n        typed_parameter_count = function_type->parameter_count;\n"""
new = """        typed_return_type = function_type->return_type;\n        typed_parameter_count = function_type->parameter_count;\n        typed_is_variadic = function_type->is_variadic;\n"""
if old in text:
    if text.count(old) != 1:
        raise SystemExit("unexpected function-type snapshot shape")
    text = text.replace(old, new, 1)
elif "typed_is_variadic = function_type->is_variadic;" not in text:
    raise SystemExit("function-type variadic snapshot missing")

old = """                                                    typed_parameter_count,\n                                                    false,\n                                                    is_internal,\n"""
new = """                                                    typed_parameter_count,\n                                                    typed_is_variadic,\n                                                    is_internal,\n"""
if old in text:
    if text.count(old) != 1:
        raise SystemExit("unexpected function-typed entity recording shape")
    text = text.replace(old, new, 1)
elif "                                                    typed_is_variadic,\n                                                    is_internal," not in text:
    raise SystemExit("function-typed entity variadic forwarding missing")
parser_path.write_text(text)

text = verifier_path.read_text()
old = """        if (function == NULL || function->is_variadic ||\n            expression->value_category != MINIC_VALUE_RVALUE ||\n"""
new = """        if (function == NULL || expression->value_category != MINIC_VALUE_RVALUE ||\n"""
if old in text:
    if text.count(old) != 1:
        raise SystemExit("unexpected function expression verifier shape")
    text = text.replace(old, new, 1)
elif "function == NULL || expression->value_category" not in text:
    raise SystemExit("variadic function expression verifier update missing")

old = """        if (function_type == NULL || function_type->parameter_count != function->parameter_count ||\n            !minic_type_equal(function_type->return_type, function->return_type)) {\n"""
new = """        if (function_type == NULL || function_type->parameter_count != function->parameter_count ||\n            function_type->is_variadic != function->is_variadic ||\n            !minic_type_equal(function_type->return_type, function->return_type)) {\n"""
if old in text:
    if text.count(old) != 1:
        raise SystemExit("unexpected function type verifier comparison shape")
    text = text.replace(old, new, 1)
elif "function_type->is_variadic != function->is_variadic" not in text:
    raise SystemExit("canonical variadic function verifier comparison missing")

old = """                                         function_type->parameter_types,\n                                         function_type->parameter_count,\n                                         false);\n"""
new = """                                         function_type->parameter_types,\n                                         function_type->parameter_count,\n                                         function_type->is_variadic);\n"""
if old in text:
    if text.count(old) != 1:
        raise SystemExit("unexpected indirect call verifier shape")
    text = text.replace(old, new, 1)
elif "                                         function_type->is_variadic);" not in text:
    raise SystemExit("indirect variadic call verifier forwarding missing")
verifier_path.write_text(text)

text = test_path.read_text()
anchor = """static int pick_first(int fixed, ...) {\n    return fixed;\n}\n\n"""
insert = """static int pick_first(int fixed, ...) {\n    return fixed;\n}\n\nint variadic_redecl(int fixed, ...);\nextern typeof(variadic_redecl) variadic_redecl;\n\n"""
if "extern typeof(variadic_redecl) variadic_redecl;" not in text:
    if text.count(anchor) != 1:
        raise SystemExit("unexpected focused test shape")
    text = text.replace(anchor, insert, 1)
test_path.write_text(text)

print("materialized function-typed variadic redeclaration and verifier fix")
