#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()

entry_old = '''    expression = minic_c0_program_expression(context->body->program, statement->expression);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
'''
entry_new = '''    expression = minic_c0_program_expression(context->body->program, statement->expression);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (context->source_function != NULL && context->source_function->name != NULL &&
        (strcmp(context->source_function->name, "shmem_parse_options") == 0 ||
         strcmp(context->source_function->name, "devpts_pty_new") == 0 ||
         strcmp(context->source_function->name, "__ext4_iget") == 0 ||
         strcmp(context->source_function->name, "hugetlbfs_parse_param") == 0)) {
        int unary_operator = -1;
        int binary_operator = -1;
        int builtin_unary_operator = -1;
        int left_kind = -1;
        int right_kind = -1;
        int operand_kind = -1;
        int call_callee_kind = -1;
        long long call_function_id = -1;
        size_t call_argument_count = 0U;
        int statement_result_kind = -1;
        if (expression->kind == MINIC_EXPRESSION_UNARY) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            unary_operator = (int)expression->value.unary.operator_kind;
            if (operand != NULL) operand_kind = (int)operand->kind;
        }
        if (expression->kind == MINIC_EXPRESSION_BINARY ||
            expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
            expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
            const MinicExpression *left = minic_c0_program_expression(
                context->body->program, expression->value.binary.left);
            const MinicExpression *right = minic_c0_program_expression(
                context->body->program, expression->value.binary.right);
            if (expression->kind == MINIC_EXPRESSION_BINARY)
                binary_operator = (int)expression->value.binary.operator_kind;
            if (left != NULL) left_kind = (int)left->kind;
            if (right != NULL) right_kind = (int)right->kind;
        }
        if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.builtin_unary.operand);
            builtin_unary_operator = (int)expression->value.builtin_unary.operator_kind;
            if (operand != NULL) operand_kind = (int)operand->kind;
        }
        if (expression->kind == MINIC_EXPRESSION_CALL) {
            const MinicExpression *callee = minic_c0_program_expression(
                context->body->program, expression->value.call.callee);
            call_function_id = expression->value.call.function_id == MINIC_FUNCTION_INVALID
                ? -1LL : (long long)expression->value.call.function_id;
            call_argument_count = expression->value.call.argument_count;
            if (callee != NULL) call_callee_kind = (int)callee->kind;
        }
        if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
            expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
            const MinicExpression *statement_result = minic_c0_program_expression(
                context->body->program, expression->value.statement_expression.result);
            if (statement_result != NULL) statement_result_kind = (int)statement_result->kind;
        }
        (void)fprintf(stderr,
                      "CORE_M129_EXPR_STMT function=%s span=%zu:%zu kind=%d value_category=%d "
                      "unary_operator=%d binary_operator=%d builtin_unary_operator=%d "
                      "left_kind=%d right_kind=%d operand_kind=%d call_function_id=%lld "
                      "call_callee_kind=%d call_argument_count=%zu type_record=%d type_integer=%d "
                      "statement_result_kind=%d\\n",
                      context->source_function->name,
                      expression->span.begin.line,
                      expression->span.begin.column,
                      (int)expression->kind,
                      (int)expression->value_category,
                      unary_operator,
                      binary_operator,
                      builtin_unary_operator,
                      left_kind,
                      right_kind,
                      operand_kind,
                      call_function_id,
                      call_callee_kind,
                      call_argument_count,
                      minic_type_is_record(expression->type) ? 1 : 0,
                      minic_type_is_integer(expression->type) ? 1 : 0,
                      statement_result_kind);
    }
'''
if 'CORE_M129_EXPR_STMT' not in text:
    if entry_old not in text:
        raise SystemExit('expression-statement entry seam changed')
    text = text.replace(entry_old, entry_new, 1)

condition_old = '''    status = lower_expression(context, expression_id, &condition);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
condition_new = '''    status = lower_expression(context, expression_id, &condition);
    if (status != MINIC_CORE_LOWER_OK) {
        int unary_operator = -1;
        int binary_operator = -1;
        int builtin_unary_operator = -1;
        int left_kind = -1;
        int right_kind = -1;
        int operand_kind = -1;
        if (expression->kind == MINIC_EXPRESSION_UNARY) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            unary_operator = (int)expression->value.unary.operator_kind;
            if (operand != NULL) operand_kind = (int)operand->kind;
        } else if (expression->kind == MINIC_EXPRESSION_BINARY) {
            const MinicExpression *left = minic_c0_program_expression(
                context->body->program, expression->value.binary.left);
            const MinicExpression *right = minic_c0_program_expression(
                context->body->program, expression->value.binary.right);
            binary_operator = (int)expression->value.binary.operator_kind;
            if (left != NULL) left_kind = (int)left->kind;
            if (right != NULL) right_kind = (int)right->kind;
        } else if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.builtin_unary.operand);
            builtin_unary_operator = (int)expression->value.builtin_unary.operator_kind;
            if (operand != NULL) operand_kind = (int)operand->kind;
        }
        (void)fprintf(stderr,
                      "CORE_M129_CONDITION_LOWER function=%s span=%zu:%zu status=%d kind=%d "
                      "unary_operator=%d binary_operator=%d builtin_unary_operator=%d "
                      "left_kind=%d right_kind=%d operand_kind=%d\\n",
                      context->source_function != NULL && context->source_function->name != NULL
                          ? context->source_function->name : "<unknown>",
                      expression->span.begin.line,
                      expression->span.begin.column,
                      (int)status,
                      (int)expression->kind,
                      unary_operator,
                      binary_operator,
                      builtin_unary_operator,
                      left_kind,
                      right_kind,
                      operand_kind);
        return status;
    }
'''
if 'CORE_M129_CONDITION_LOWER' not in text:
    if condition_old not in text:
        raise SystemExit('condition lower seam changed')
    text = text.replace(condition_old, condition_new, 1)

flat_old = '''            status = lower_block(context, &normalized_do_while_body, &body_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
'''
flat_new = '''            status = lower_block(context, &normalized_do_while_body, &body_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                (void)fprintf(stderr,
                              "CORE_M129_DO_WHILE_FLAT_BODY function=%s span=%zu:%zu status=%d\\n",
                              context->source_function != NULL && context->source_function->name != NULL
                                  ? context->source_function->name : "<unknown>",
                              statement->span.begin.line,
                              statement->span.begin.column,
                              (int)status);
                return status;
            }
'''
if 'CORE_M129_DO_WHILE_FLAT_BODY' not in text:
    if flat_old not in text:
        raise SystemExit('do-while flatten seam changed')
    text = text.replace(flat_old, flat_new, 1)

old = '''            statement_expression_terminated = false;
            block_status = lower_block(context, statement_block, &statement_expression_terminated);
            if (block_status != MINIC_CORE_LOWER_OK || statement_expression_terminated ||
                expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
                return block_status;
            }
'''
new = '''            (void)fprintf(stderr,
                "CORE_M127_VOID_STMT_EXPR_ENTER function=%s span=%zu:%zu block_id=%llu "
                "result_id=%llu block_statements=%zu\\n",
                context->source_function->name,
                expression->span.begin.line,
                expression->span.begin.column,
                (unsigned long long)expression->value.statement_expression.block,
                (unsigned long long)expression->value.statement_expression.result,
                statement_block->statement_count);
            statement_expression_terminated = false;
            block_status = lower_block(context, statement_block, &statement_expression_terminated);
            (void)fprintf(stderr,
                "CORE_M127_VOID_STMT_EXPR_EXIT function=%s span=%zu:%zu block_id=%llu "
                "result_id=%llu block_status=%d terminated=%d\\n",
                context->source_function->name,
                expression->span.begin.line,
                expression->span.begin.column,
                (unsigned long long)expression->value.statement_expression.block,
                (unsigned long long)expression->value.statement_expression.result,
                (int)block_status,
                statement_expression_terminated ? 1 : 0);
            if (block_status != MINIC_CORE_LOWER_OK || statement_expression_terminated ||
                expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
                return block_status;
            }
'''
if 'CORE_M127_VOID_STMT_EXPR_ENTER' in text:
    raise SystemExit('M127 census already staged')
if old not in text:
    raise SystemExit('void statement-expression block seam changed')
text = text.replace(old, new, 1)
path.write_text(text)
print('M127/M129 leaf-owner census staged')
