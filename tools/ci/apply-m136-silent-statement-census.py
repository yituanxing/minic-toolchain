#!/usr/bin/env python3
from pathlib import Path
import re

# M136_TRIGGER_V4: attribute the post-M135 silent failure across every nested
# terminated-block caller and the final Core verifier boundary.
path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M136_SILENT_STATEMENT_CENSUS'
if marker in text:
    print('M136 silent statement census already staged')
    raise SystemExit(0)

selector = '''getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
                    context->source_function != NULL && context->source_function->name != NULL &&
                    (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
                     strcmp(context->source_function->name, "membarrier_global_expedited") == 0)'''

needle = '''        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (block_terminated) {
'''
replacement = '''        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M136_SILENT_STATEMENT_CENSUS: CI-only trace for post-M135 failures. */
        if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
            context->source_function != NULL && context->source_function->name != NULL &&
            (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
             strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {
            (void)fprintf(stderr,
                          "CORE_M136_STMT_ENTRY function=%s statement_id=%llu kind=%d "
                          "span=%zu:%zu cleanup=%llu stop=%llu terminated=%d "
                          "expression=%llu target_expression=%llu then=%llu else=%llu\\n",
                          context->source_function->name,
                          (unsigned long long)source_block->statements[statement_index],
                          (int)statement->kind,
                          statement->span.begin.line,
                          statement->span.begin.column,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context,
                          block_terminated ? 1 : 0,
                          (unsigned long long)statement->expression,
                          (unsigned long long)statement->target_expression,
                          (unsigned long long)statement->then_block,
                          (unsigned long long)statement->else_block);
        }
        if (block_terminated) {
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one lower_block statement-entry seam, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)

# Direct/asm-goto re-entry diagnostics keep proving whether M134 itself is the
# rejector. Absence of these lines is also useful evidence.
needle = '''        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->target_statement < program->statement_count &&
            statement_membership[source->target_statement]) {
            unsafe = true;
            break;
        }
'''
replacement = '''        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->target_statement < program->statement_count &&
            statement_membership[source->target_statement]) {
            if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
                context->source_function != NULL && context->source_function->name != NULL &&
                (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
                 strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {
                (void)fprintf(stderr,
                              "CORE_M136_REENTRY function=%s source_id=%zu source_kind=%d "
                              "source_span=%zu:%zu target_id=%llu target_kind=%d "
                              "target_span=%zu:%zu root_kind=%d root_span=%zu:%zu\\n",
                              context->source_function->name, source_index, (int)source->kind,
                              source->span.begin.line, source->span.begin.column,
                              (unsigned long long)source->target_statement,
                              (int)program->statements[source->target_statement].kind,
                              program->statements[source->target_statement].span.begin.line,
                              program->statements[source->target_statement].span.begin.column,
                              (int)root_statement->kind,
                              root_statement->span.begin.line, root_statement->span.begin.column);
            }
            unsafe = true;
            break;
        }
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one unreachable goto re-entry seam, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)

needle = '''                if (target < program->statement_count && statement_membership[target]) {
                    unsafe = true;
                    break;
                }
'''
replacement = '''                if (target < program->statement_count && statement_membership[target]) {
                    if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
                        context->source_function != NULL && context->source_function->name != NULL &&
                        (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
                         strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {
                        (void)fprintf(stderr,
                                      "CORE_M136_REENTRY function=%s source_id=%zu source_kind=%d "
                                      "source_span=%zu:%zu asm_label=%zu target_id=%llu "
                                      "target_kind=%d target_span=%zu:%zu root_kind=%d root_span=%zu:%zu\\n",
                                      context->source_function->name, source_index, (int)source->kind,
                                      source->span.begin.line, source->span.begin.column, label_index,
                                      (unsigned long long)target,
                                      (int)program->statements[target].kind,
                                      program->statements[target].span.begin.line,
                                      program->statements[target].span.begin.column,
                                      (int)root_statement->kind,
                                      root_statement->span.begin.line, root_statement->span.begin.column);
                    }
                    unsafe = true;
                    break;
                }
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one unreachable asm-goto re-entry seam, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)

# Instrument every simple nested-block termination rejection independent of
# indentation. Keep the original semantic result unchanged.
pattern = re.compile(r'(?P<i>^[ \t]*)if \(terminated\) \{\n(?P=i)    return MINIC_CORE_LOWER_UNSUPPORTED;\n(?P=i)\}', re.M)

def repl(match):
    i = match.group('i')
    return (f'{i}if (terminated) {{\n'
            f'{i}    if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&\n'
            f'{i}        context->source_function != NULL && context->source_function->name != NULL &&\n'
            f'{i}        (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||\n'
            f'{i}         strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {{\n'
            f'{i}        (void)fprintf(stderr, "CORE_M136_TERMINATED_REJECT function=%s owner_line=%d\\n",\n'
            f'{i}                      context->source_function->name, __LINE__);\n'
            f'{i}    }}\n'
            f'{i}    return MINIC_CORE_LOWER_UNSUPPORTED;\n'
            f'{i}}}')
text, terminated_count = pattern.subn(repl, text)
if terminated_count < 3:
    raise SystemExit(f'expected >=3 simple terminated rejection seams, found {terminated_count}')

# Effect-only GNU statement-expression has a combined legacy guard that can
# return OK while the nested block is terminated. Trace this separately.
needle = '''            block_status = lower_block(context, statement_block, &statement_expression_terminated);
            if (block_status != MINIC_CORE_LOWER_OK || statement_expression_terminated ||
                expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
                return block_status;
            }
'''
replacement = '''            block_status = lower_block(context, statement_block, &statement_expression_terminated);
            if (block_status != MINIC_CORE_LOWER_OK || statement_expression_terminated ||
                expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
                if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
                    context->source_function != NULL && context->source_function->name != NULL &&
                    (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
                     strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {
                    (void)fprintf(stderr,
                                  "CORE_M136_STMTEXPR_GUARD function=%s status=%d terminated=%d result_invalid=%d span=%zu:%zu\\n",
                                  context->source_function->name, (int)block_status,
                                  statement_expression_terminated ? 1 : 0,
                                  expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID ? 1 : 0,
                                  expression->span.begin.line, expression->span.begin.column);
                }
                return block_status;
            }
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one effect-only statement-expression guard, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)

# Final function phase/verify attribution.
needle = '''    terminated = false;
    if (status == MINIC_CORE_LOWER_OK) status = lower_block(&context, source_block, &terminated);
    free(statement_blocks); free(local_objects);
'''
replacement = '''    terminated = false;
    if (status == MINIC_CORE_LOWER_OK) status = lower_block(&context, source_block, &terminated);
    if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
        (strcmp(source_function->name, "get_nohz_timer_target") == 0 ||
         strcmp(source_function->name, "membarrier_global_expedited") == 0)) {
        (void)fprintf(stderr,
                      "CORE_M136_FUNCTION_EXIT function=%s status=%d terminated=%d block=%llu\\n",
                      source_function->name, (int)status, terminated ? 1 : 0,
                      (unsigned long long)context.block_id);
    }
    free(statement_blocks); free(local_objects);
'''
if text.count(needle) != 1:
    raise SystemExit('function lower_block phase seam changed')
text = text.replace(needle, replacement, 1)

needle = '''    if (!minic_core_function_verify(&lowered)) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
'''
replacement = '''    if (!minic_core_function_verify(&lowered)) {
        if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
            (strcmp(source_function->name, "get_nohz_timer_target") == 0 ||
             strcmp(source_function->name, "membarrier_global_expedited") == 0)) {
            (void)fprintf(stderr, "CORE_M136_VERIFY_FAIL function=%s\\n", source_function->name);
        }
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
'''
if text.count(needle) != 1:
    raise SystemExit('Core verifier seam changed')
text = text.replace(needle, replacement, 1)

path.write_text(text)
print(f'M136 silent statement census staged; terminated_reject_seams={terminated_count}')
