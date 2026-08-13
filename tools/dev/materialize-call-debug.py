#!/usr/bin/env python3
from pathlib import Path

path = Path('src/target/riscv64/codegen_expression.c')
text = path.read_text()
anchor = '''    case MINIC_EXPRESSION_CALL: {
        const MinicFunction *callee;
'''
insert = '''    case MINIC_EXPRESSION_CALL: {
        const MinicFunction *callee;
'''
if text.count(anchor) != 1:
    raise SystemExit('call case anchor missing')
# Insert debug after callee resolution instead of changing case shape.
old = '''        if (callee == NULL) {
            return false;
        }
'''
new = '''        if (callee == NULL) {
            return false;
        }
        if (function != NULL && function->name != NULL &&
            strcmp(function->name, "mapped_fsuid") == 0) {
            size_t debug_index;

            fprintf(stderr,
                    "CALL_DEBUG caller=%s callee=%s expr=%zu args=%zu params=%zu return_base=%d return_ptr=%u\\n",
                    function->name,
                    callee->name,
                    (size_t)expression_id,
                    expression->value.call.argument_count,
                    callee->parameter_count,
                    (int)expression->type.base_kind,
                    expression->type.pointer_depth);
            for (debug_index = 0U; debug_index < expression->value.call.argument_count; ++debug_index) {
                const MinicExpression *debug_argument;

                debug_argument = minic_c0_program_expression(
                    program, expression->value.call.arguments[debug_index]);
                fprintf(stderr,
                        "CALL_ARG index=%zu expr=%zu kind=%d category=%d base=%d ptr=%u record=%zu\\n",
                        debug_index,
                        (size_t)expression->value.call.arguments[debug_index],
                        debug_argument == NULL ? -1 : (int)debug_argument->kind,
                        debug_argument == NULL ? -1 : (int)debug_argument->value_category,
                        debug_argument == NULL ? -1 : (int)debug_argument->type.base_kind,
                        debug_argument == NULL ? 0U : debug_argument->type.pointer_depth,
                        debug_argument == NULL ? (size_t)-1 : debug_argument->type.record_id);
            }
        }
'''
# There are multiple callee==NULL guards; anchor on nearby call-case context by replacing first after call case position.
pos = text.find(anchor)
idx = text.find(old, pos)
if idx < 0:
    raise SystemExit('callee guard after call case missing')
text = text[:idx] + new + text[idx+len(old):]
path.write_text(text)
