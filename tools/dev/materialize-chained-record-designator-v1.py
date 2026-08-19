#!/usr/bin/env python3
"""Materialize runtime chained record designator support on the current frontend."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "src/frontend/parser_statement.c"

helper = '''static bool parse_runtime_record_designator_target(MinicParser *parser,
                                                   MinicExpressionId base_id,
                                                   MinicExpressionId *target_id) {
    MinicExpressionId current_id;

    if (parser == NULL || target_id == NULL || parser->current.kind != MINIC_TOKEN_DOT) {
        minic_parser_error(parser, "expected designated record initializer");
        return false;
    }
    current_id = base_id;
    do {
        MinicExpressionId member_id;

        if (!minic_parser_parse_direct_member(parser, current_id, &member_id)) {
            return false;
        }
        current_id = member_id;
    } while (parser->current.kind == MINIC_TOKEN_DOT);
    *target_id = current_id;
    return true;
}

'''

function_anchor = '''bool minic_parser_parse_runtime_record_initializer(MinicParser *parser,
                                                   MinicExpressionId target_id) {
'''

old = '''        if (parser->current.kind != MINIC_TOKEN_DOT ||
            !minic_parser_parse_direct_member(parser, target_id, &member_id) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_EQUAL, "expected '=' after record designator")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "expected designated record initializer");
            }
            return false;
        }
'''

new = '''        if (!parse_runtime_record_designator_target(parser, target_id, &member_id) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_EQUAL, "expected '=' after record designator")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "expected designated record initializer");
            }
            return false;
        }
'''

text = PATH.read_text()
if helper not in text:
    if text.count(function_anchor) != 1:
        raise SystemExit("runtime record initializer function anchor changed")
    text = text.replace(function_anchor, helper + function_anchor, 1)
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("runtime record designator parse anchor changed")
    text = text.replace(old, new, 1)
PATH.write_text(text)
print("materialized runtime chained record designator path")
