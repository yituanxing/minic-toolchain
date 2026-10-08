#!/usr/bin/env python3
"""Diagnostic-only: print the actual AST kind and source for rejected GNU choose."""
from pathlib import Path
p=Path("src/frontend/parser_expression.c")
s=p.read_text()
start=s.index("parse_builtin_choose_expr(MinicParser *parser,")
end=s.index("static bool object_extent_direct_object(",start)
chunk=s[start:end]
anchor='''        minic_parser_error(
            parser, "__builtin_choose_expr condition must be an integer constant expression");'''
injection=r'''        {
            const MinicExpression *dbg =
                minic_c0_program_expression(parser->program, condition_id);
            const MinicExpression *operand = NULL;
            size_t pos = dbg != NULL ? dbg->span.begin.offset : 0U;
            size_t len = dbg != NULL && dbg->span.end.offset >= pos
                ? dbg->span.end.offset-pos : 0U;
            if (dbg != NULL && dbg->kind == MINIC_EXPRESSION_BUILTIN_UNARY) {
                operand = minic_c0_program_expression(
                    parser->program, dbg->value.builtin_unary.operand);
            }
            fprintf(stderr,
                    "CHOOSE_AST_DIAG id=%u kind=%d integer_kind=%d builtin_kind=%d "
                    "builtin_op=%d operand_kind=%d is_int=%d source=%.*s\n",
                    (unsigned)condition_id, dbg != NULL ? (int)dbg->kind : -1,
                    (int)MINIC_EXPRESSION_INTEGER,
                    (int)MINIC_EXPRESSION_BUILTIN_UNARY,
                    dbg != NULL && dbg->kind == MINIC_EXPRESSION_BUILTIN_UNARY
                        ? (int)dbg->value.builtin_unary.operator_kind : -1,
                    operand != NULL ? (int)operand->kind : -1,
                    dbg != NULL ? (int)minic_type_is_integer(dbg->type) : 0,
                    (int)(len < 180U ? len : 180U),
                    dbg != NULL ? parser->source+pos : "");
        }
'''
if chunk.count(anchor)!=1:raise SystemExit(f"debug anchor count={chunk.count(anchor)}")
p.write_text(s[:start]+chunk.replace(anchor,injection+anchor)+s[end:])
print("CHOOSE_AST_DIAGNOSTIC=APPLIED")
