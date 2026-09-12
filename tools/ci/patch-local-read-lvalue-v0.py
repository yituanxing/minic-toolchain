#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
old = '''    if (expression->kind == MINIC_EXPRESSION_LOCAL &&
        expression->value_category == MINIC_VALUE_RVALUE) {
        *local_id = expression->value.local_id;
        return true;
    }
'''
new = '''    /* Normalized Core value use may pass the LOCAL lvalue expression directly;
       lower_expression() is already the value-producing boundary.  Address
       formation goes through lower_address() instead, so accepting this shape
       does not turn an assignment target into a constant read. */
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        *local_id = expression->value.local_id;
        return true;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one local-read anchor, found {text.count(old)}")
p.write_text(text.replace(old, new, 1))
print("MINIC_LOCAL_LVALUE_READ_PATCH=APPLIED")
