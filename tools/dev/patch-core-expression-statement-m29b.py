#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    target_id = expression->value.binary.left;
'''
new = '''    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
        MinicCoreValueId discarded_value;
        MinicType discarded_type;

        if (!core_scalar_expression_value_type(context->body, expression, &discarded_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)discarded_type;
        return lower_expression(context, statement->expression, &discarded_value);
    }
    target_id = expression->value.binary.left;
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"M29b expression-statement anchor count={count}, expected 1")
path.write_text(text.replace(old, new, 1))
print("M29B_PATCH_APPLIED")
