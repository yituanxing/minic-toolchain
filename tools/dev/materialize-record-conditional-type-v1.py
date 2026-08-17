#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/expression_semantics.c"
text = path.read_text()

old = '''    if (minic_c0_expression_is_null_pointer_constant_v0(program, when_true_expression_id) &&\n        minic_type_is_pointer(when_false->type)) {\n        *result = when_false->type;\n        return true;\n    }\n    return conditional_type_only(target, when_true->type, when_false->type, result);\n}\n'''
new = '''    if (minic_c0_expression_is_null_pointer_constant_v0(program, when_true_expression_id) &&\n        minic_type_is_pointer(when_false->type)) {\n        *result = when_false->type;\n        return true;\n    }\n    if (minic_type_is_record(when_true->type) && minic_type_is_record(when_false->type) &&\n        minic_c0_types_compatible(program, when_true->type, when_false->type)) {\n        /* The conditional expression is an rvalue. Keep the common record identity while\n         * dropping lvalue-only top-level qualification from either source arm. */\n        return minic_type_unqualified(when_true->type, result) && minic_type_is_record(*result);\n    }\n    return conditional_type_only(target, when_true->type, when_false->type, result);\n}\n'''

if text.count(old) != 1:
    raise SystemExit(f"conditional result anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
