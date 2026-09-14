#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

old = '''    if ((expression->kind == MINIC_EXPRESSION_CAST ||
         expression->kind == MINIC_EXPRESSION_CONVERSION) &&
        minic_type_is_integer(expression->type)) {
'''
new = '''    if ((expression->kind == MINIC_EXPRESSION_CAST ||
         expression->kind == MINIC_EXPRESSION_BITCAST ||
         expression->kind == MINIC_EXPRESSION_CONVERSION) &&
        minic_type_is_integer(expression->type)) {
'''

count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one M181 pointer-to-integer cast gate, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_POINTER_BITCAST_INTEGER_CFG_V0=APPLIED")
