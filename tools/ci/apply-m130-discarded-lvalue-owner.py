#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
text = core_path.read_text()
marker = 'M130_DISCARDED_LVALUE_EFFECT_OWNER'

if marker not in text:
    needle = '''        if (operand == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_expression(context, expression->value.unary.operand, &discarded_value);\n'''
    if needle not in text:
        raise SystemExit('discard lowering seam changed')
    replacement = '''        if (operand == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        /* M130_DISCARDED_LVALUE_EFFECT_OWNER: a non-volatile lvalue whose value\n           is explicitly discarded does not need an rvalue load, but evaluating\n           the lvalue can still have effects through its base/index expression.\n           Ask the established address owner to perform exactly that evaluation.\n           Only claim shapes that are genuinely addressable; unsupported\n           bit-fields or other special lvalues continue through the old value\n           path. Volatile lvalues also stay on the value path so their observable\n           read is preserved. */\n        if (operand->value_category == MINIC_VALUE_LVALUE &&\n            !minic_type_is_volatile(operand->type)) {\n            status = lower_address(\n                context, expression->value.unary.operand, &discarded_value);\n            if (status == MINIC_CORE_LOWER_OK) {\n                *value_id = MINIC_CORE_VALUE_INVALID;\n                return MINIC_CORE_LOWER_OK;\n            }\n            if (status == MINIC_CORE_LOWER_ERROR) {\n                return status;\n            }\n        }\n        status = lower_expression(context, expression->value.unary.operand, &discarded_value);\n'''
    text = text.replace(needle, replacement, 1)
    core_path.write_text(text)

Path('tests/compiler/c0/m130_discarded_lvalue.c').write_text('''typedef struct {\n    int value;\n} item_t;\n\nstatic item_t global_item = {7};\n\nstatic item_t *touch_item(int *count) {\n    *count += 1;\n    return &global_item;\n}\n\nint main(void) {\n    int count = 0;\n    (void)touch_item(&count)->value;\n    return count == 1 ? 0 : 1;\n}\n''')

print('M130 discarded-lvalue effect owner and strict regression staged')
