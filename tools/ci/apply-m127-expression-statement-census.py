#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
old = '''            return lower_expression(context,\n                                    expression->value.statement_expression.result,\n                                    &discarded_value);\n'''
new = '''            {\n                MinicCoreLowerStatus result_status;\n\n                result_status = lower_expression(\n                    context, expression->value.statement_expression.result, &discarded_value);\n                if (result_status != MINIC_CORE_LOWER_OK) {\n                    (void)fprintf(\n                        stderr,\n                        "CORE_M127_VOID_STMT_EXPR_RESULT function=%s status=%d "\n                        "result_kind=%d value_category=%d span=%zu:%zu block_statements=%zu\\n",\n                        context->source_function->name,\n                        (int)result_status,\n                        (int)statement_result->kind,\n                        (int)statement_result->value_category,\n                        statement_result->span.begin.line,\n                        statement_result->span.begin.column,\n                        statement_block->statement_count);\n                }\n                return result_status;\n            }\n'''
if 'CORE_M127_VOID_STMT_EXPR_RESULT' in text:
    raise SystemExit('M127 census already staged')
if old not in text:
    raise SystemExit('void statement-expression result seam changed')
text = text.replace(old, new, 1)
path.write_text(text)
print('M127 expression-statement census staged')
