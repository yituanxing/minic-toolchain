#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
old = '''            statement_expression_terminated = false;
            block_status = lower_block(context, statement_block, &statement_expression_terminated);
            if (block_status != MINIC_CORE_LOWER_OK || statement_expression_terminated ||
                expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
                return block_status;
            }
'''
new = '''            (void)fprintf(
                stderr,
                "CORE_M127_VOID_STMT_EXPR_ENTER function=%s span=%zu:%zu "
                "block_id=%llu result_id=%llu block_statements=%zu\\n",
                context->source_function->name,
                expression->span.begin.line,
                expression->span.begin.column,
                (unsigned long long)expression->value.statement_expression.block,
                (unsigned long long)expression->value.statement_expression.result,
                statement_block->statement_count);
            statement_expression_terminated = false;
            block_status = lower_block(context, statement_block, &statement_expression_terminated);
            (void)fprintf(
                stderr,
                "CORE_M127_VOID_STMT_EXPR_EXIT function=%s span=%zu:%zu "
                "block_id=%llu result_id=%llu block_status=%d terminated=%d\\n",
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
print('M127 nested statement-expression census staged')
