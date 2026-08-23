from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M60_POINTER_CONDITIONAL_VALUE'
if marker in text:
    print('M60 pointer conditional value already applied')
    raise SystemExit(0)

anchor = '''        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !minic_type_is_integer(expression->type) || minic_type_is_const(expression->type) ||
            minic_type_is_volatile(expression->type)) {
'''
replacement = '''        /* M60_POINTER_CONDITIONAL_VALUE: C conditional values may be pointer
           scalars as well as integers. The existing arm conversion, spill and
           reload machinery is already scalar-generic, so keep the semantic
           restriction at the Core scalar boundary rather than at integer-only. */
        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !core_memory_scalar_type(expression->type) || minic_type_is_const(expression->type) ||
            minic_type_is_volatile(expression->type)) {
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M60 anchor count={text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
path.write_text(text)
print('M60 pointer conditional value applied')
