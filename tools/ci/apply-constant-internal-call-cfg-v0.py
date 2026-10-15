#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
old = '''    if (callee == NULL || !callee->is_defined || !callee->is_internal || !callee->is_inline ||\n        !minic_type_is_integer(callee->return_type) || callee->body_block == MINIC_BLOCK_INVALID) {\n'''
new = '''    if (callee == NULL || !callee->is_defined || !callee->is_internal ||\n        !minic_type_is_integer(callee->return_type) || callee->body_block == MINIC_BLOCK_INVALID) {\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one constant-call eligibility guard, found {count}")
text = text.replace(old, new, 1)
p.write_text(text)
print("MINIC_CONSTANT_INTERNAL_CALL_CFG_V0=APPLIED")
