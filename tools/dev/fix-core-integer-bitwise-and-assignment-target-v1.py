#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = """            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||\n            !minic_type_integer_common(stored_type, source->type, &common_type)) {\n"""
new = """            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||\n            context->target == NULL ||\n            !minic_target_info_integer_common_for_program(context->target,\n                                                          context->body->program,\n                                                          stored_type,\n                                                          source->type,\n                                                          &common_type)) {\n"""
if text.count(old) != 1:
    raise SystemExit(f"expected one old common-integer owner call, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("routed M14 usual integer conversions through TargetInfo owner")
