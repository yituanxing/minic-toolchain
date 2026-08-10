#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

# Parser: a GNU statement-expression without a final expression has type void.
path = root / "src/frontend/parser_statement.c"
text = path.read_text()
old = r'''    block = block_id < parser->program->block_count ? &parser->program->blocks[block_id] : NULL;
    last_statement = NULL;
    result = NULL;
    if (success && (block == NULL || block->statement_count == 0U)) {
        minic_parser_error(parser,
                           "GNU statement expression currently requires a final expression");
        success = false;
    }
    if (success) {
        last_statement_id = block->statements[block->statement_count - 1U];
        last_statement = minic_c0_program_statement(parser->program, last_statement_id);
        if (last_statement == NULL || last_statement->kind != MINIC_STATEMENT_EXPRESSION ||
            last_statement->expression == MINIC_EXPRESSION_INVALID) {
            minic_parser_error(
                parser,
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
                  minic_parser_expect(
                      parser, MINIC_TOKEN_RBRACE, "expected '}' in GNU statement expression");
    }
'''
new = r'''    block = block_id < parser->program->block_count ? &parser->program->blocks[block_id] : NULL;
    last_statement = NULL;
    result = NULL;
    last_statement_id = MINIC_STATEMENT_INVALID;
    if (success && block == NULL) {
        minic_parser_error(parser, "invalid GNU statement-expression block");
        success = false;
    }
    if (success && block->statement_count != 0U) {
        last_statement_id = block->statements[block->statement_count - 1U];
        last_statement = minic_c0_program_statement(parser->program, last_statement_id);
        if (last_statement != NULL && last_statement->kind == MINIC_STATEMENT_EXPRESSION &&
            last_statement->expression != MINIC_EXPRESSION_INVALID) {
            result = minic_c0_program_expression(parser->program, last_statement->expression);
            if (result == NULL) {
                minic_parser_error(parser, "invalid GNU statement-expression result");
                success = false;
            }
        }
    }
    if (success) {
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_STATEMENT;
        expression.span.begin = begin;
        expression.span.end = parser->current.span.end;
        expression.type = result == NULL ? minic_type_void() : result->type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.statement_expression.block = block_id;
        expression.value.statement_expression.result =
            result == NULL ? MINIC_EXPRESSION_INVALID : last_statement->expression;
        if (result != NULL) {
            block->statement_count -= 1U;
        }
        success = minic_parser_add_expression(parser, &expression, expression_id) &&
                  minic_parser_expect(
                      parser, MINIC_TOKEN_RBRACE, "expected '}' in GNU statement expression");
    }
'''
text = replace_once(text, old, new, "void-statement-expression-parser")
path.write_text(text)

# Verifier: INVALID result is the canonical representation for a void statement-expression.
path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
old = r'''        block = minic_c0_program_block(program, expression->value.statement_expression.block);
        result = expression_before(
            program, expression->value.statement_expression.result, expression_index);
        return block != NULL && result != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, result->type);
'''
new = r'''        block = minic_c0_program_block(program, expression->value.statement_expression.block);
        if (block == NULL || expression->value_category != MINIC_VALUE_RVALUE) {
            return false;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return minic_type_is_void(expression->type);
        }
        result = expression_before(
            program, expression->value.statement_expression.result, expression_index);
        return result != NULL && minic_type_equal(expression->type, result->type);
'''
text = replace_once(text, old, new, "void-statement-expression-verifier")
path.write_text(text)

# Cast normalization leaves the INVALID void result untouched.
path = root / "src/frontend/cast_normalization.c"
text = path.read_text()
old = r'''    case MINIC_EXPRESSION_STATEMENT:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.statement_expression.result,
                                   &expression->value.statement_expression.result);
'''
new = r'''    case MINIC_EXPRESSION_STATEMENT:
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return minic_type_is_void(expression->type);
        }
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.statement_expression.result,
                                   &expression->value.statement_expression.result);
'''
text = replace_once(text, old, new, "void-statement-expression-normalization")
path.write_text(text)

# RV64: execute the owned block; only emit a value when one exists.
path = root / "src/target/riscv64/codegen_expression.c"
text = path.read_text()
old = r'''        label_counter = label_stride + expression_id * label_stride;
        return minic_riscv64_emit_block(file,
                                        program,
                                        function,
                                        expression->value.statement_expression.block,
                                        &label_counter) &&
               minic_riscv64_emit_expression(
                   file, program, function, expression->value.statement_expression.result);
'''
new = r'''        label_counter = label_stride + expression_id * label_stride;
        if (!minic_riscv64_emit_block(file,
                                      program,
                                      function,
                                      expression->value.statement_expression.block,
                                      &label_counter)) {
            return false;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return minic_type_is_void(expression->type);
        }
        return minic_riscv64_emit_expression(
            file, program, function, expression->value.statement_expression.result);
'''
text = replace_once(text, old, new, "void-statement-expression-codegen")
path.write_text(text)

# Existing gate now covers both value and void statement-expression ownership.
path = root / "tests/compiler/c0/gnu_statement_expression.c"
path.write_text(r'''static int statement_value(int input) {
    int value = input;

    return ({
        do {
            value += 2;
        } while (0);
        value;
    });
}

static int statement_void(int input) {
    int value = input;

    ({
        value += 3;
        __asm__ __volatile__("" : : : "memory");
    });
    return value;
}

int main(void) {
    return statement_value(5) == 7 && statement_void(4) == 7 ? 0 : 1;
}
''')

path = root / "tests/compiler/c0/run-gnu-statement-expression.sh"
text = path.read_text()
text = replace_once(
    text,
    "grep -F 'statement_value:' \"$assembly\" >/dev/null\n",
    "grep -F 'statement_value:' \"$assembly\" >/dev/null\n"
    "grep -F 'statement_void:' \"$assembly\" >/dev/null\n",
    "void-statement-expression-assembly-check",
)
text = replace_once(
    text,
    "scope=owned-block final-expression=value sequencing=expression-site loop=1",
    "scope=owned-block final-expression=value void-final=non-expression sequencing=expression-site loop=1",
    "void-statement-expression-summary",
)
path.write_text(text)
