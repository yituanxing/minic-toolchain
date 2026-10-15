from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M62_POINTER_RETURN_CONVERSION'
if marker in text:
    print('M62 pointer return conversion already applied')
    raise SystemExit(0)

anchor = '''        } else if (minic_type_is_pointer(context->source_function->return_type)) {
            status = lower_expression(context, statement->expression, &terminator.return_value);
            if (status == MINIC_CORE_LOWER_OK &&
                (terminator.return_value >= context->function->value_count ||
                 !minic_type_equal(context->function->values[terminator.return_value].type,
                                   context->source_function->return_type))) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
replacement = '''        } else if (minic_type_is_pointer(context->source_function->return_type)) {
            /* M62_POINTER_RETURN_CONVERSION: return uses assignment conversion.
               In particular, T * may return as volatile T * / const T * without
               requiring the source expression to already carry the exact pointer
               qualifiers. Reuse the scalar assignment seam rather than imposing
               an exact-type Core artifact at the return boundary. */
            status = lower_scalar_assignment_value(context,
                                                   context->source_function->return_type,
                                                   statement->expression,
                                                   &terminator.return_value);
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M62 anchor count={text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
path.write_text(text)
print('M62 pointer return conversion applied')
