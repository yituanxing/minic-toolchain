#!/usr/bin/env python3
"""Evaluate GNU __builtin_choose_expr ICE before parsing the two result arms.

The condition is semantically independent of both result expressions.
Keeping its ConstEval result across arm parsing avoids use of a temporary
expression ID after the parser has visited unrelated arm expressions.
"""
from pathlib import Path
p=Path("src/frontend/parser_expression.c")
s=p.read_text()
start=s.index("parse_builtin_choose_expr(MinicParser *parser,")
end=s.index("static bool object_extent_direct_object(",start)
f=s[start:end]
old='''        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected first ',' in __builtin_choose_expr") ||
        !parse_expression_internal(parser, &when_true_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected second ',' in __builtin_choose_expr") ||
        !parse_expression_internal(parser, &when_false_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_choose_expr")) {
        return false;
    }
    if (!minic_const_eval_integer(
            parser->program, parser->target_info, condition_id, &condition_value) ||
        !minic_const_value_is_zero(
            parser->program, parser->target_info, &condition_value, &condition_is_zero)) {
        minic_parser_error(
            parser, "__builtin_choose_expr condition must be an integer constant expression");
        return false;
    }
'''
changed='''        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected first ',' in __builtin_choose_expr")) {
        return false;
    }
    /* Evaluate immediately: the selected arm cannot affect whether the
       condition is an integer constant expression. Keep the boolean, not
       a borrowed expression ID, across subsequent arm parsing. */
    if (!minic_const_eval_integer(
            parser->program, parser->target_info, condition_id, &condition_value) ||
        !minic_const_value_is_zero(
            parser->program, parser->target_info, &condition_value, &condition_is_zero)) {
        minic_parser_error(
            parser, "__builtin_choose_expr condition must be an integer constant expression");
        return false;
    }
    if (!parse_expression_internal(parser, &when_true_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected second ',' in __builtin_choose_expr") ||
        !parse_expression_internal(parser, &when_false_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_choose_expr")) {
        return false;
    }
'''
if f.count(old)!=1:
    raise SystemExit(f"choose-eager patch anchor count={f.count(old)}")
p.write_text(s[:start]+f.replace(old,changed)+s[end:])
print("GNU_CHOOSE_EAGER_CONDITION_V1=APPLIED")
