#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
old = '''            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d\\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1);
            return false;
'''
new = '''            const MinicExpression *failed_expression;

            failed_expression = statement != NULL &&
                                        statement->expression != MINIC_EXPRESSION_INVALID
                                    ? minic_c0_program_expression(program, statement->expression)
                                    : NULL;
            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d "
                    "expression=%zu expression_kind=%d target=%zu\\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1,
                    statement != NULL ? (size_t)statement->expression : (size_t)-1,
                    failed_expression != NULL ? (int)failed_expression->kind : -1,
                    statement != NULL ? (size_t)statement->target_expression : (size_t)-1);
            if (failed_expression != NULL) {
                switch (failed_expression->kind) {
                case MINIC_EXPRESSION_ASSIGNMENT:
                    fprintf(stderr,
                            "CODEGEN_FAIL_EXPR assignment left=%zu right=%zu type_kind=%d\\n",
                            (size_t)failed_expression->value.binary.left,
                            (size_t)failed_expression->value.binary.right,
                            (int)failed_expression->type.kind);
                    break;
                case MINIC_EXPRESSION_CALL:
                    fprintf(stderr,
                            "CODEGEN_FAIL_EXPR call function=%zu callee=%zu argc=%zu\\n",
                            (size_t)failed_expression->value.call.function_id,
                            (size_t)failed_expression->value.call.callee,
                            failed_expression->value.call.argument_count);
                    break;
                case MINIC_EXPRESSION_BINARY:
                    fprintf(stderr,
                            "CODEGEN_FAIL_EXPR binary op=%d left=%zu right=%zu\\n",
                            (int)failed_expression->value.binary.operator_kind,
                            (size_t)failed_expression->value.binary.left,
                            (size_t)failed_expression->value.binary.right);
                    break;
                case MINIC_EXPRESSION_UNARY:
                    fprintf(stderr,
                            "CODEGEN_FAIL_EXPR unary op=%d operand=%zu\\n",
                            (int)failed_expression->value.unary.operator_kind,
                            (size_t)failed_expression->value.unary.operand);
                    break;
                case MINIC_EXPRESSION_CONDITIONAL:
                    fprintf(stderr,
                            "CODEGEN_FAIL_EXPR conditional condition=%zu true=%zu false=%zu\\n",
                            (size_t)failed_expression->value.conditional.condition,
                            (size_t)failed_expression->value.conditional.when_true,
                            (size_t)failed_expression->value.conditional.when_false);
                    break;
                default:
                    break;
                }
            }
            return false;
'''
if text.count(old) != 1:
    raise SystemExit("unexpected codegen failure diagnostic block")
path.write_text(text.replace(old, new, 1))
print("staged linenoise codegen expression diagnostics")
