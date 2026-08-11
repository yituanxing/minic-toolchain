#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_enum.c"
text = path.read_text()
old = '''    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant_value) ||
        !const_value_to_numeric(parser, &constant_value, value) ||
        !enum_value_type(parser, value, value_type) ||
        !normalize_value_bits(parser, value, *value_type, bits)) {
        minic_parser_error(parser, "enum initializer must be a representable integer constant expression");
        return false;
    }
    return true;
'''
new = '''    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant_value)) {
        minic_parser_error(parser, "enum initializer must be an integer constant expression");
        return false;
    }
    if (!const_value_to_numeric(parser, &constant_value, value) ||
        !enum_value_type(parser, value, value_type) ||
        !normalize_value_bits(parser, value, *value_type, bits)) {
        minic_parser_error(parser, "enum initializer exceeds the supported 64-bit value range");
        return false;
    }
    return true;
'''
if text.count(old) != 1:
    raise SystemExit(f"enum diagnostic split anchor: expected 1, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
