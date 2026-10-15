#!/usr/bin/env python3
from pathlib import Path
import re

# M138_TRIGGER_V1: synchronize after workflow registration.
path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M138_ERROR_OWNER_TRACE'
if marker in text:
    print('M138 error owner trace already staged')
    raise SystemExit(0)

# Trace the precise argument expression shape when the existing direct-call
# owner reports an argument-lowering ERROR. Keep this diagnostic target-neutral
# and do not change the lowering result.
call_pattern = re.compile(
    r'(?P<indent>        )if \(status != MINIC_CORE_LOWER_OK\) \{\n'
    r'(?P<body>            \(void\)fprintf\(stderr,\n'
    r'                          "CORE_LOWER_DETAIL marker=BATCH_D_VARIADIC_DIRECT_CALL function=%s "\n'
    r'                          "stage=direct-call callee=%s arg=%zu fixed=%d reason=argument-lower status=%d\\n",\n'
    r'.*?\n        \})',
    re.S,
)
match = call_pattern.search(text)
if match is None:
    raise SystemExit('M138 direct-call diagnostic seam changed')
body = match.group('body')
new = '''        if (status != MINIC_CORE_LOWER_OK) {
            /* M138_ERROR_OWNER_TRACE: expose the failing call-argument AST leaf. */
            if (getenv("CORE_M138_ERROR_TRACE") != NULL &&
                context->source_function != NULL && context->source_function->name != NULL &&
                (strcmp(context->source_function->name, "__readahead_batch") == 0 ||
                 strcmp(context->source_function->name, "wait_task_inactive") == 0)) {
                MinicExpressionId m138_arg_id = expression->value.call.arguments[argument_index];
                const MinicExpression *m138_arg =
                    minic_c0_program_expression(context->body->program, m138_arg_id);
                (void)fprintf(stderr,
                              "CORE_M138_CALL_ARG function=%s callee=%s arg=%zu status=%d id=%llu kind=%d vc=%d span=%zu:%zu\\n",
                              context->source_function->name, callee_name, argument_index,
                              (int)status, (unsigned long long)m138_arg_id,
                              m138_arg != NULL ? (int)m138_arg->kind : -1,
                              m138_arg != NULL ? (int)m138_arg->value_category : -1,
                              m138_arg != NULL ? m138_arg->span.begin.line : 0U,
                              m138_arg != NULL ? m138_arg->span.begin.column : 0U);
                if (m138_arg != NULL && m138_arg->kind == MINIC_EXPRESSION_ADDRESS_OF) {
                    MinicExpressionId m138_operand_id = m138_arg->value.unary.operand;
                    const MinicExpression *m138_operand =
                        minic_c0_program_expression(context->body->program, m138_operand_id);
                    (void)fprintf(stderr,
                                  "CORE_M138_CALL_ARG_OPERAND function=%s id=%llu kind=%d vc=%d span=%zu:%zu\\n",
                                  context->source_function->name,
                                  (unsigned long long)m138_operand_id,
                                  m138_operand != NULL ? (int)m138_operand->kind : -1,
                                  m138_operand != NULL ? (int)m138_operand->value_category : -1,
                                  m138_operand != NULL ? m138_operand->span.begin.line : 0U,
                                  m138_operand != NULL ? m138_operand->span.begin.column : 0U);
                }
            }
''' + body + '\n'
text = text[:match.start()] + new + text[match.end():]

# Attribute every lower_while ERROR/UNSUPPORTED exit with a stable site number.
start_anchor = 'static MinicCoreLowerStatus\nlower_while(MinicCoreLowerContext *context,'
end_anchor = '\nstatic bool core_inline_asm_constraint_is'
start = text.find(start_anchor)
end = text.find(end_anchor, start)
if start < 0 or end < 0:
    raise SystemExit('M138 lower_while region changed')
region = text[start:end]
site = 0
pattern = re.compile(r'(?P<indent>^[ \t]*)return (?P<status>MINIC_CORE_LOWER_(?:ERROR|UNSUPPORTED));', re.M)

def replace_return(match):
    global site
    site += 1
    indent = match.group('indent')
    status = match.group('status')
    line_start = region.rfind('\n', 0, match.start()) + 1
    line_end = region.find('\n', match.end())
    if line_end < 0:
        line_end = len(region)
    snippet = region[line_start:line_end].strip()
    print(f'M138_WHILE_SITE site={site} status={status} source={snippet}')
    return (f'{indent}if (getenv("CORE_M138_ERROR_TRACE") != NULL &&\n'
            f'{indent}    context->source_function != NULL && context->source_function->name != NULL &&\n'
            f'{indent}    strcmp(context->source_function->name, "wait_task_inactive") == 0) {{\n'
            f'{indent}    (void)fprintf(stderr,\n'
            f'{indent}                  "CORE_M138_WHILE_RETURN function=%s site={site} status={status} statement_kind=%d expression=%llu span=%zu:%zu\\n",\n'
            f'{indent}                  context->source_function->name, (int)statement->kind,\n'
            f'{indent}                  (unsigned long long)statement->expression,\n'
            f'{indent}                  statement->span.begin.line, statement->span.begin.column);\n'
            f'{indent}}}\n'
            f'{indent}return {status};')

region, count = pattern.subn(replace_return, region)
if count < 2:
    raise SystemExit(f'M138 expected multiple lower_while exits, found {count}')
text = text[:start] + region + text[end:]
path.write_text(text)
print(f'M138 error owner trace staged; while_sites={count}')
