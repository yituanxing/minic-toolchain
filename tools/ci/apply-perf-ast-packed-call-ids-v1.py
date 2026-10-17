#!/usr/bin/env python3
"""Single-factor, disposable-checkout 32-bit CALL-argument ID packing.

Experimental ONLY: preserve the fixed inline 64-slot array, semantic IDs, and
AST shallow value-copy safety; reduce payload bytes 512 -> 256. Do not add
heap ownership, per-CALL mallocs, or change AST growth factor (PERF-033 NO).
No production C is modified in the repository by this tool's addition.
"""
from pathlib import Path

def edit(path, changes):
    p=Path(path)
    s=p.read_text()
    for old,new in changes:
        n=s.count(old)
        if n!=1:
            raise SystemExit("PACKED_AST_AB_ANCHOR_MISMATCH %s occurrences=%d %r"%
                             (path,n,old[:110]))
        s=s.replace(old,new,1)
    p.write_text(s)

edit("src/frontend/ast.h", [
  ("MinicExpressionId arguments[MINIC_MAX_FUNCTION_PARAMETERS];",
   "uint32_t arguments[MINIC_MAX_FUNCTION_PARAMETERS];"),
  ("} MinicExpression;\n",
   '''} MinicExpression;

/* Experimental representation: 32-bit IDs in the 64 inline CALL slots.
 * Every produced expression ID is bounded by UINT32_MAX in ast.c. Avoid
 * lossy conversion if malformed external users pass a larger ID. */
static inline bool minic_c0_call_set_argument(MinicExpression *expression,
                                              size_t index,
                                              MinicExpressionId id) {
    if (expression == NULL || index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        id > UINT32_MAX) {
        return false;
    }
    expression->value.call.arguments[index] = (uint32_t)id;
    return true;
}
''')
])
edit("src/frontend/ast.c", [
 ('''bool minic_c0_program_add_expression(MinicC0Program *program,
                                     const MinicExpression *expression,
                                     MinicExpressionId *expression_id) {
    if (!minic_grow_array(''',
  '''bool minic_c0_program_add_expression(MinicC0Program *program,
                                     const MinicExpression *expression,
                                     MinicExpressionId *expression_id) {
    /* A call argument must fit the packed representation; do not wrap. */
    if (program->expression_count >= (size_t)UINT32_MAX) {
        return false;
    }
    if (!minic_grow_array(''')
])
edit("src/frontend/parser_expression.c",[
  ('''    if (argument_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_parser_parse_expression(
            parser, &call_expression->value.call.arguments[argument_index], 0U)) {
        return false;
    }
    argument_id = call_expression->value.call.arguments[argument_index];''',
  '''    if (argument_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_parser_parse_expression(parser, &argument_id, 0U) ||
        !minic_c0_call_set_argument(call_expression, argument_index, argument_id)) {
        return false;
    }'''),
  ('''        call_expression->value.call.arguments[argument_index] = argument_id;
        argument = minic_c0_program_expression(parser->program, argument_id);''',
  '''        if (!minic_c0_call_set_argument(call_expression, argument_index, argument_id)) {
            return false;
        }
        argument = minic_c0_program_expression(parser->program, argument_id);'''),
  # same form appears twice: after first replacement, exactly one remains
  ('''        call_expression->value.call.arguments[argument_index] = argument_id;
        argument = minic_c0_program_expression(parser->program, argument_id);''',
  '''        if (!minic_c0_call_set_argument(call_expression, argument_index, argument_id)) {
            return false;
        }
        argument = minic_c0_program_expression(parser->program, argument_id);''')
])
edit("src/frontend/parser_postfix.c",[
  ("        call->value.call.arguments[argument_index] = argument_id;",
   "        if (!minic_c0_call_set_argument(call, argument_index, argument_id)) return false;"),
  ("            call->value.call.arguments[argument_index++] = argument_id;",
   """            if (!minic_c0_call_set_argument(call, argument_index, argument_id)) return false;
            ++argument_index;""")
])
edit("src/frontend/parser_statement.c",[
 ("    call.value.call.arguments[0] = address_id;",
  '''    if (!minic_c0_call_set_argument(&call, 0U, address_id)) {
        minic_parser_error(parser, "cannot represent GNU cleanup call argument");
        return false;
    }''')
])
edit("src/frontend/ast_traversal.c",[
 ('''        for (argument_index = 0U; argument_index < expression->value.call.argument_count;
             ++argument_index) {
            if (!visit_expression_id(
                    &expression->value.call.arguments[argument_index], visitor, context)) {
                return false;
            }
        }
        return true;''',
  '''        for (argument_index = 0U; argument_index < expression->value.call.argument_count;
             ++argument_index) {
            MinicExpressionId argument_id =
                expression->value.call.arguments[argument_index];
            if (!visit_expression_id(&argument_id, visitor, context) ||
                !minic_c0_call_set_argument(expression, argument_index, argument_id)) {
                return false;
            }
        }
        return true;''')
])
print("MINIC_AST_PACKED_CALL_IDS=APPLIED fixed_inline_64=true heap_payloads=0")
