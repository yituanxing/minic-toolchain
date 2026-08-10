#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    "    MINIC_EXPRESSION_CONDITIONAL,\n    MINIC_EXPRESSION_CALL\n",
    "    MINIC_EXPRESSION_CONDITIONAL,\n    MINIC_EXPRESSION_CALL,\n    MINIC_EXPRESSION_STATEMENT\n",
    "statement-expression-kind",
)
replace_once(
    "src/frontend/ast.h",
    """        struct {
            MinicExpressionId condition;
            MinicExpressionId when_true;
            MinicExpressionId when_false;
        } conditional;
""",
    """        struct {
            MinicExpressionId condition;
            MinicExpressionId when_true;
            MinicExpressionId when_false;
        } conditional;
        struct {
            MinicBlockId block;
            MinicExpressionId result;
        } statement_expression;
""",
    "statement-expression-payload",
)
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);\n",
    """bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);
bool minic_parser_parse_statement_expression(MinicParser *parser,
                                             MinicSourcePosition begin,
                                             MinicExpressionId *expression_id);
""",
    "statement-expression-prototype",
)

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
anchor = "static bool parse_branch(MinicParser *parser, MinicBlockId *block_id) {\n"
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
        if (parser != NULL) {
            minic_parser_error(parser, "expected '{' in GNU statement expression");
        }
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
    last_statement = NULL;
    result = NULL;
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
    raise SystemExit(f"statement-expression-parser: expected one branch anchor, found {text.count(anchor)}")
path.write_text(text.replace(anchor, helper + anchor, 1))

# Locate the parenthesized-primary arm *inside parse_primary* instead of matching
# the whole function text, because earlier typeof/_Generic discovery rewrites
# this function as well.
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
start = text.find("static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {")
end = text.find("\nstatic bool local_array_without_array_type", start)
if start < 0 or end < 0:
    raise SystemExit("statement-expression-primary: cannot locate parse_primary")
body = text[start:end]
arm_start = body.rfind("    if (parser->current.kind == MINIC_TOKEN_LPAREN) {")
end_marker = "        return finish_value_expression(parser, primary_id, decay_array, expression_id);\n    }\n"
arm_end = body.find(end_marker, arm_start)
if arm_start < 0 or arm_end < 0:
    raise SystemExit("statement-expression-primary: cannot locate parenthesized primary arm")
arm_end += len(end_marker)
replacement = r'''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicSourcePosition begin;

        begin = parser->current.span.begin;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            if (!minic_parser_parse_statement_expression(parser, begin, &primary_id) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_RPAREN,
                                     "expected ')' after GNU statement expression")) {
                return false;
            }
        } else if (!parse_expression_internal(parser, &primary_id, 0U, decay_array) ||
                   !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
            return false;
        }
        if (!minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
'''
body = body[:arm_start] + replacement + body[arm_end:]
path.write_text(text[:start] + body + text[end:])

replace_once(
    "src/frontend/cast_normalization.c",
    "    case MINIC_EXPRESSION_CALL:\n",
    """    case MINIC_EXPRESSION_STATEMENT:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.statement_expression.result,
                                   &expression->value.statement_expression.result);
    case MINIC_EXPRESSION_CALL:
""",
    "statement-expression-normalization",
)
replace_once(
    "src/frontend/ast_verifier.c",
    "    case MINIC_EXPRESSION_CALL:\n",
    """    case MINIC_EXPRESSION_STATEMENT: {
        const MinicBlock *block;
        const MinicExpression *result;

        block = minic_c0_program_block(program, expression->value.statement_expression.block);
        result = expression_before(
            program, expression->value.statement_expression.result, expression_index);
        return block != NULL && result != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, result->type);
    }
    case MINIC_EXPRESSION_CALL:
""",
    "statement-expression-verifier",
)
replace_once(
    "src/target/riscv64/codegen_expression.c",
    "    case MINIC_EXPRESSION_CALL: {\n",
    """    case MINIC_EXPRESSION_STATEMENT: {
        size_t label_stride;
        size_t label_counter;

        if (program->statement_count == SIZE_MAX) {
            return false;
        }
        label_stride = program->statement_count + 1U;
        if (expression_id > (SIZE_MAX - label_stride) / label_stride) {
            return false;
        }
        label_counter = label_stride + expression_id * label_stride;
        return minic_riscv64_emit_block(file,
                                        program,
                                        function,
                                        expression->value.statement_expression.block,
                                        &label_counter) &&
               minic_riscv64_emit_expression(
                   file, program, function, expression->value.statement_expression.result);
    }
    case MINIC_EXPRESSION_CALL: {
""",
    "statement-expression-codegen",
)

print("staged GNU statement expressions with structural primary patching and owned evaluation block")
