#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


# GNU statement expressions must remain an expression node with an owned block.
# Hoisting the contained statements into the surrounding block would break
# sequencing when the construct is nested in &&, ?:, calls, or other expressions.
replace_once(
    "src/frontend/ast.h",
    '''    MINIC_EXPRESSION_CONDITIONAL,\n    MINIC_EXPRESSION_CALL\n''',
    '''    MINIC_EXPRESSION_CONDITIONAL,\n    MINIC_EXPRESSION_CALL,\n    MINIC_EXPRESSION_STATEMENT\n''',
)
replace_once(
    "src/frontend/ast.h",
    '''        struct {\n            MinicExpressionId condition;\n            MinicExpressionId when_true;\n            MinicExpressionId when_false;\n        } conditional;\n''',
    '''        struct {\n            MinicExpressionId condition;\n            MinicExpressionId when_true;\n            MinicExpressionId when_false;\n        } conditional;\n        struct {\n            MinicBlockId block;\n            MinicExpressionId result;\n        } statement_expression;\n''',
)

# Publish the statement parser entry used by primary-expression parsing.
replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);\n''',
    '''bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);\nbool minic_parser_parse_statement_expression(MinicParser *parser,\n                                             MinicSourcePosition begin,\n                                             MinicExpressionId *expression_id);\n''',
)

# Parse `({ ...; final_expression; })` in its own lexical scope and AST block.
# The final expression statement is detached from the block because its value is
# emitted exactly once as the result of the enclosing expression.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
anchor = '''static bool parse_branch(MinicParser *parser, MinicBlockId *block_id) {\n'''
helper = r'''bool minic_parser_parse_statement_expression(MinicParser *parser,
                                             MinicSourcePosition begin,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicBlock *block;
    const MinicExpression *result;
    const MinicStatement *last_statement;
    MinicBlockId block_id;
    MinicBlockId parent_block;
    MinicStatementId last_statement_id;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected '{' in GNU statement expression");
        return false;
    }
    parent_block = parser->current_block;
    if (!minic_c0_program_add_block(parser->program, &block_id) ||
        !minic_parser_begin_scope(parser)) {
        minic_parser_error(parser, "cannot create GNU statement-expression scope");
        return false;
    }
    parser->current_block = block_id;
    success = minic_parser_advance(parser);
    while (success && parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of GNU statement expression");
            success = false;
            break;
        }
        success = minic_parser_parse_statement(parser, true);
    }

    block = block_id < parser->program->block_count ? &parser->program->blocks[block_id] : NULL;
    if (success && (block == NULL || block->statement_count == 0U)) {
        minic_parser_error(parser, "GNU statement expression currently requires a final expression");
        success = false;
    }
    if (success) {
        last_statement_id = block->statements[block->statement_count - 1U];
        last_statement = minic_c0_program_statement(parser->program, last_statement_id);
        if (last_statement == NULL || last_statement->kind != MINIC_STATEMENT_EXPRESSION ||
            last_statement->expression == MINIC_EXPRESSION_INVALID) {
            minic_parser_error(parser,
                               "GNU statement expression currently requires an expression as its final statement");
            success = false;
        }
    }
    if (success) {
        result = minic_c0_program_expression(parser->program, last_statement->expression);
        if (result == NULL) {
            minic_parser_error(parser, "invalid GNU statement-expression result");
            success = false;
        }
    }
    if (success) {
        block->statement_count -= 1U;
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_STATEMENT;
        expression.span.begin = begin;
        expression.span.end = parser->current.span.end;
        expression.type = result->type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.statement_expression.block = block_id;
        expression.value.statement_expression.result = last_statement->expression;
        success = minic_parser_add_expression(parser, &expression, expression_id) &&
                  minic_parser_expect(parser,
                                      MINIC_TOKEN_RBRACE,
                                      "expected '}' in GNU statement expression");
    }

    parser->current_block = parent_block;
    minic_parser_end_scope(parser);
    return success;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"statement-expression parser anchor: expected one match, found {text.count(anchor)}")
path.write_text(text.replace(anchor, helper + anchor, 1))

# Detect `({` before ordinary parenthesized-expression parsing. The helper leaves
# the parser on the closing `)`, after which postfix parsing proceeds normally.
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
old = '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n        if (!minic_parser_advance(parser) ||\n            !parse_expression_internal(parser, &primary_id, 0U, decay_array) ||\n            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {\n            return false;\n        }\n        if (!minic_parser_parse_postfix(parser, primary_id, &primary_id)) {\n            return false;\n        }\n        return finish_value_expression(parser, primary_id, decay_array, expression_id);\n    }\n'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n        MinicSourcePosition begin;\n\n        begin = parser->current.span.begin;\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n        if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n            if (!minic_parser_parse_statement_expression(parser, begin, &primary_id) ||\n                !minic_parser_expect(parser,\n                                     MINIC_TOKEN_RPAREN,\n                                     "expected ')' after GNU statement expression")) {\n                return false;\n            }\n        } else if (!parse_expression_internal(parser, &primary_id, 0U, decay_array) ||\n                   !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {\n            return false;\n        }\n        if (!minic_parser_parse_postfix(parser, primary_id, &primary_id)) {\n            return false;\n        }\n        return finish_value_expression(parser, primary_id, decay_array, expression_id);\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"primary parenthesis anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Preserve the result edge when cast normalization rewrites expression IDs.
path = Path("src/frontend/cast_normalization.c")
text = path.read_text()
anchor = '''    case MINIC_EXPRESSION_CALL:\n'''
case = '''    case MINIC_EXPRESSION_STATEMENT:\n        return remap_expression_id(mapping,\n                                   old_expression_count,\n                                   current_old_index,\n                                   expression->value.statement_expression.result,\n                                   &expression->value.statement_expression.result);\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"normalization statement-expression anchor: expected one match, found {text.count(anchor)}")
path.write_text(text.replace(anchor, case + anchor, 1))

# The AST verifier makes the block/result ownership explicit. The final result
# must precede the wrapper expression and have the same semantic type.
path = Path("src/frontend/ast_verifier.c")
text = path.read_text()
anchor = '''    case MINIC_EXPRESSION_CALL:\n'''
case = '''    case MINIC_EXPRESSION_STATEMENT: {\n        const MinicBlock *block;\n        const MinicExpression *result;\n\n        block = minic_c0_program_block(program, expression->value.statement_expression.block);\n        result = expression_before(\n            program, expression->value.statement_expression.result, expression_index);\n        return block != NULL && result != NULL &&\n               expression->value_category == MINIC_VALUE_RVALUE &&\n               minic_type_equal(expression->type, result->type);\n    }\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"AST verifier statement-expression anchor: expected one match, found {text.count(anchor)}")
path.write_text(text.replace(anchor, case + anchor, 1))

# During the direct-AST RV64 bootstrap, emit the statement-expression block at
# the exact point where the expression is evaluated, then emit its final value.
# Use an expression-derived label range so nested control flow cannot collide
# with function-body labels. Core IR will later make this sequencing explicit.
path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
anchor = '''    case MINIC_EXPRESSION_CALL: {\n'''
case = '''    case MINIC_EXPRESSION_STATEMENT: {\n        size_t label_stride;\n        size_t label_counter;\n\n        if (program->statement_count == SIZE_MAX) {\n            return false;\n        }\n        label_stride = program->statement_count + 1U;\n        if (expression_id > (SIZE_MAX - label_stride) / label_stride) {\n            return false;\n        }\n        label_counter = label_stride + expression_id * label_stride;\n        return minic_riscv64_emit_block(file,\n                                        program,\n                                        function,\n                                        expression->value.statement_expression.block,\n                                        &label_counter) &&\n               minic_riscv64_emit_expression(\n                   file, program, function, expression->value.statement_expression.result);\n    }\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"RV64 statement-expression anchor: expected one match, found {text.count(anchor)}")
path.write_text(text.replace(anchor, case + anchor, 1))

print("staged GNU statement expressions with owned scope/block, verifier, normalization and RV64 sequencing")
