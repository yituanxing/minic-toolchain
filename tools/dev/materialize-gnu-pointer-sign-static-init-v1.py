#!/usr/bin/env python3
"""Materialize shared GNU pointer-sign compatibility for calls and static initializers."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TYPE_H = ROOT / "src/frontend/type.h"
TYPE_C = ROOT / "src/frontend/type.c"
EXPR_C = ROOT / "src/frontend/parser_expression.c"
GLOBAL_C = ROOT / "src/frontend/parser_global.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if text.count(old) != 1:
        raise SystemExit(f"{label}: expected one anchor, found {text.count(old)}")
    return text.replace(old, new, 1)


# The relation belongs to the type layer. Contexts decide whether they consume it.
type_h = TYPE_H.read_text()
type_h = replace_once(
    type_h,
    "bool minic_type_assignment_compatible(MinicType target, MinicType source);\n",
    "bool minic_type_assignment_compatible(MinicType target, MinicType source);\n"
    "bool minic_type_gnu_pointer_sign_compatible(MinicType target, MinicType source);\n",
    "type.h declaration",
)
TYPE_H.write_text(type_h)

relation = r'''bool minic_type_gnu_pointer_sign_compatible(MinicType target, MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;

    if (target.pointer_depth != 1U || source.pointer_depth != 1U ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee) || !minic_type_is_integer(target_pointee) ||
        !minic_type_is_integer(source_pointee) ||
        target_pointee.integer_rank != source_pointee.integer_rank) {
        return false;
    }
    /* GNU C accepts pointer-sign differences as a diagnosable conversion. Keep
       representation-changing rank differences rejected, and never discard
       pointee qualifiers. Plain/signed/unsigned char remain one byte-oriented
       compatibility family, matching the existing call-conversion behavior. */
    if (target_pointee.integer_rank != MINIC_INTEGER_RANK_CHAR &&
        (target_pointee.integer_sign == source_pointee.integer_sign ||
         target_pointee.is_plain_char != source_pointee.is_plain_char)) {
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

'''
type_c = TYPE_C.read_text()
anchor = "bool minic_type_conditional_pointer_common(MinicType left, MinicType right, MinicType *result) {\n"
if relation not in type_c:
    if type_c.count(anchor) != 1:
        raise SystemExit("type.c relation anchor changed")
    type_c = type_c.replace(anchor, relation + anchor, 1)
TYPE_C.write_text(type_c)

# Remove the parser-private copy so the semantic relation has one owner.
expr_c = EXPR_C.read_text()
if "static bool pointer_sign_call_conversion_compatible" in expr_c:
    pattern = re.compile(
        r"static bool pointer_sign_call_conversion_compatible\(MinicType target, MinicType source\) \{.*?\n\}\n\n",
        re.DOTALL,
    )
    expr_c, count = pattern.subn("", expr_c, count=1)
    if count != 1:
        raise SystemExit("parser_expression.c pointer-sign helper shape changed")
expr_c = expr_c.replace(
    "pointer_sign_call_conversion_compatible(target_type, source->type)",
    "minic_type_gnu_pointer_sign_compatible(target_type, source->type)",
)
if "pointer_sign_call_conversion_compatible" in expr_c:
    raise SystemExit("parser_expression.c still owns pointer-sign compatibility")
EXPR_C.write_text(expr_c)

# Static initialization is another GNU conversion context, but ordinary assignment
# remains governed by minic_c0_assignment_compatible and stays fail-closed.
global_c = GLOBAL_C.read_text()
context_helper = r'''static bool static_pointer_initializer_type_compatible(const MinicParser *parser,
                                                       MinicType target_type,
                                                       MinicExpressionId expression_id) {
    const MinicExpression *source;

    if (parser == NULL) {
        return false;
    }
    if (minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        return true;
    }
    source = minic_c0_program_expression(parser->program, expression_id);
    return source != NULL &&
           minic_type_gnu_pointer_sign_compatible(target_type, source->type);
}

'''
parse_anchor = "static bool parse_static_pointer_initializer(MinicParser *parser,\n"
if context_helper not in global_c:
    if global_c.count(parse_anchor) != 1:
        raise SystemExit("parser_global.c static pointer initializer anchor changed")
    global_c = global_c.replace(parse_anchor, context_helper + parse_anchor, 1)
global_c = replace_once(
    global_c,
    "    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {\n"
    "        minic_parser_error(parser, \"static pointer initializer type mismatch\");\n"
    "        return false;\n"
    "    }\n",
    "    if (!static_pointer_initializer_type_compatible(parser, target_type, expression_id)) {\n"
    "        minic_parser_error(parser, \"static pointer initializer type mismatch\");\n"
    "        return false;\n"
    "    }\n",
    "parser_global.c compatibility check",
)
GLOBAL_C.write_text(global_c)

print("materialized shared GNU pointer-sign compatibility for call/static-init contexts")
