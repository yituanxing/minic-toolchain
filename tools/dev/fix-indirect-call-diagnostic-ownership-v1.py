#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_postfix.c")
text = path.read_text()

old = '''        if (!minic_parser_parse_expression(parser, &argument_id, 0U) ||
            !minic_parser_apply_fixed_call_argument_conversion(
                parser, function_type->parameter_types[argument_index], &argument_id) ||
            !minic_c0_fixed_call_argument_compatible(
                parser->program, function_type->parameter_types[argument_index], argument_id)) {
            minic_parser_error(parser, "indirect call argument type does not match declaration");
            return false;
        }
'''
new = '''        if (!minic_parser_parse_expression(parser, &argument_id, 0U)) {
            return false;
        }
        if (!minic_parser_apply_fixed_call_argument_conversion(
                parser, function_type->parameter_types[argument_index], &argument_id)) {
            if (parser->diagnostic == NULL || parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "indirect call argument type does not match declaration");
            }
            return false;
        }
        if (!minic_c0_fixed_call_argument_compatible(
                parser->program, function_type->parameter_types[argument_index], argument_id)) {
            minic_parser_error(parser, "indirect call argument type does not match declaration");
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected fixed indirect argument parser shape")
text = text.replace(old, new, 1)

old = '''            if (argument_index >= MINIC_MAX_FUNCTION_PARAMETERS || !minic_parser_advance(parser) ||
                !minic_parser_parse_expression(parser, &argument_id, 0U) ||
                !minic_parser_apply_array_decay(parser, argument_id, &argument_id)) {
                minic_parser_error(parser,
                                   "variadic call argument count exceeds implementation limit");
                return false;
            }
'''
new = '''            if (argument_index >= MINIC_MAX_FUNCTION_PARAMETERS) {
                minic_parser_error(parser,
                                   "variadic call argument count exceeds implementation limit");
                return false;
            }
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_expression(parser, &argument_id, 0U)) {
                return false;
            }
            if (!minic_parser_apply_array_decay(parser, argument_id, &argument_id)) {
                if (parser->diagnostic == NULL || parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, "unsupported variadic call argument type");
                }
                return false;
            }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected variadic indirect argument parser shape")
text = text.replace(old, new, 1)
path.write_text(text)
print("materialized indirect-call diagnostic ownership fix")
