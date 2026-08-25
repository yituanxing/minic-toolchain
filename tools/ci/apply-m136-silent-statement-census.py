#!/usr/bin/env python3
from pathlib import Path

# M136_TRIGGER_V3: attribute silent post-termination rejection to its caller.
path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M136_SILENT_STATEMENT_CENSUS'
if marker in text:
    print('M136 silent statement census already staged')
    raise SystemExit(0)

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
                              context->source_function->name,
                              source_index,
                              (int)source->kind,
                              source->span.begin.line,
                              source->span.begin.column,
                              (unsigned long long)source->target_statement,
                              (int)program->statements[source->target_statement].kind,
                              program->statements[source->target_statement].span.begin.line,
                              program->statements[source->target_statement].span.begin.column,
                              (int)root_statement->kind,
                              root_statement->span.begin.line,
                              root_statement->span.begin.column);
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
                                      context->source_function->name,
                                      source_index,
                                      (int)source->kind,
                                      source->span.begin.line,
                                      source->span.begin.column,
                                      label_index,
                                      (unsigned long long)target,
                                      (int)program->statements[target].kind,
                                      program->statements[target].span.begin.line,
                                      program->statements[target].span.begin.column,
                                      (int)root_statement->kind,
                                      root_statement->span.begin.line,
                                      root_statement->span.begin.column);
                    }
                    unsafe = true;
                    break;
                }
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one unreachable asm-goto re-entry seam, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)

# Several older aggregate/statement-expression owners accept lower_block() only
# when the nested block falls through. Attribute those silent rejections by the
# exact source line before deciding which ownership rule is obsolete.
needle = '''            if (terminated) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
count = text.count(needle)
if count == 0:
    raise SystemExit('expected at least one nested terminated rejection seam')
replacement = '''            if (terminated) {
                if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
                    context->source_function != NULL && context->source_function->name != NULL &&
                    (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
                     strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {
                    (void)fprintf(stderr,
                                  "CORE_M136_TERMINATED_REJECT function=%s owner_line=%d\\n",
                                  context->source_function->name, __LINE__);
                }
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
text = text.replace(needle, replacement)

path.write_text(text)
print(f'M136 silent statement census staged; terminated_reject_seams={count}')
