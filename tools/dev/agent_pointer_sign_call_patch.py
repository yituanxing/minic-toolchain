#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Fixed-parameter call conversions are shared by direct and indirect calls. This
# is intentionally narrower than assignment compatibility: GCC accepts a
# one-level integer-pointer signedness mismatch as a diagnosed call conversion,
# while ordinary incompatible-pointer assignments remain rejected by MiniC.
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_apply_array_decay(MinicParser *parser,\n"
    "                                    MinicExpressionId input_id,\n"
    "                                    MinicExpressionId *expression_id);\n",
    "bool minic_parser_apply_fixed_call_argument_conversion(MinicParser *parser,\n"
    "                                                       MinicType target_type,\n"
    "                                                       MinicExpressionId *argument_id);\n"
    "bool minic_parser_apply_array_decay(MinicParser *parser,\n"
    "                                    MinicExpressionId input_id,\n"
    "                                    MinicExpressionId *expression_id);\n",
    "shared-call-conversion-prototype",
)

p = Path("src/frontend/parser_expression.c")
text = p.read_text()
old = '''static bool apply_fixed_call_argument_conversion(MinicParser *parser,
                                                 MinicType target_type,
                                                 MinicExpressionId *argument_id) {
    const MinicExpression *source;
    MinicExpression conversion;
    MinicExpressionId source_id;
    MinicSourceSpan source_span;

    if (parser == NULL || argument_id == NULL) {
        return false;
    }
    source_id = *argument_id;
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL) {
        minic_parser_error(parser, "invalid call argument conversion source");
        return false;
    }
    if (minic_c0_assignment_compatible(parser->program, target_type, source_id)) {
        return true;
    }
    if (!minic_type_is_double(target_type) || !minic_type_is_integer(source->type)) {
        return true;
    }
    source_span = source->span;

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = source_span;
    conversion.type = target_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = source_id;
    return minic_parser_add_expression(parser, &conversion, argument_id);
}
'''
new = r'''static bool pointer_sign_call_conversion_compatible(MinicType target, MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;

    if (target.pointer_depth != 1U || source.pointer_depth != 1U ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee) ||
        !minic_type_is_integer(target_pointee) || !minic_type_is_integer(source_pointee) ||
        target_pointee.integer_rank != source_pointee.integer_rank ||
        target_pointee.integer_sign == source_pointee.integer_sign ||
        target_pointee.is_plain_char != source_pointee.is_plain_char) {
        return false;
    }
    if (minic_type_is_const(source_pointee) && !minic_type_is_const(target_pointee)) {
        return false;
    }
    if (minic_type_is_volatile(source_pointee) && !minic_type_is_volatile(target_pointee)) {
        return false;
    }
    return true;
}

bool minic_parser_apply_fixed_call_argument_conversion(MinicParser *parser,
                                                       MinicType target_type,
                                                       MinicExpressionId *argument_id) {
    const MinicExpression *source;
    MinicExpression conversion;
    MinicExpressionId source_id;
    MinicSourceSpan source_span;
    bool needs_explicit_conversion;

    if (parser == NULL || argument_id == NULL) {
        return false;
    }
    source_id = *argument_id;
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL) {
        minic_parser_error(parser, "invalid call argument conversion source");
        return false;
    }
    if (minic_c0_assignment_compatible(parser->program, target_type, source_id)) {
        return true;
    }
    needs_explicit_conversion =
        (minic_type_is_double(target_type) && minic_type_is_integer(source->type)) ||
        pointer_sign_call_conversion_compatible(target_type, source->type);
    if (!needs_explicit_conversion) {
        return true;
    }
    source_span = source->span;

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = source_span;
    conversion.type = target_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = source_id;
    return minic_parser_add_expression(parser, &conversion, argument_id);
}
'''
if text.count(old) != 1:
    raise SystemExit(f"direct call conversion helper: expected one anchor, found {text.count(old)}")
text = text.replace(old, new, 1)
text = text.replace("apply_fixed_call_argument_conversion(\n", "minic_parser_apply_fixed_call_argument_conversion(\n")
p.write_text(text)

# Indirect fixed-parameter calls consume the same conversion contract before
# running the ordinary assignment-compatible verifier.
replace_once(
    "src/frontend/parser_postfix.c",
    '''        argument = minic_c0_program_expression(parser->program, argument_id);
        if (argument == NULL ||
            !minic_c0_assignment_compatible(
                parser->program, function_type->parameter_types[argument_index], argument_id)) {
            minic_parser_error(parser, "indirect call argument type does not match declaration");
            return false;
        }
        call->value.call.arguments[argument_index] = argument_id;
''',
    '''        argument = minic_c0_program_expression(parser->program, argument_id);
        if (argument == NULL ||
            !minic_parser_apply_fixed_call_argument_conversion(
                parser, function_type->parameter_types[argument_index], &argument_id) ||
            !minic_c0_assignment_compatible(
                parser->program, function_type->parameter_types[argument_index], argument_id)) {
            minic_parser_error(parser, "indirect call argument type does not match declaration");
            return false;
        }
        call->value.call.arguments[argument_index] = argument_id;
''',
    "indirect-call-conversion",
)
