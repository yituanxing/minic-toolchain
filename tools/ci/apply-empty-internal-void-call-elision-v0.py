#!/usr/bin/env python3
from pathlib import Path

# Elide a direct call only when the callee is an internal inline void helper
# whose body is empty (or begins with an unconditional `return;`) and every
# argument is side-effect-free according to the established CFG purity check.
# This preserves C argument side effects by refusing any impure argument while
# avoiding useless symbol materialization such as &dma_dummy_ops passed to a
# CONFIG-off empty set_dma_ops().
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M186_EMPTY_INTERNAL_VOID_CALL_ELISION"
if marker in text:
    print("MINIC_EMPTY_INTERNAL_VOID_CALL_ELISION_V0=ALREADY")
    raise SystemExit(0)

fn = "static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,\n"
start = text.find(fn)
if start < 0:
    raise SystemExit("lower_direct_call definition missing")
end = text.find("\nstatic ", start + len(fn))
if end < 0:
    raise SystemExit("lower_direct_call end missing")
body = text[start:end]
anchor = '''    if (argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!callee->is_variadic && argument_count != callee->parameter_count) ||
        (callee->is_variadic && argument_count < callee->parameter_count) ||
        (!returns_void && !core_memory_scalar_type(callee->return_type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
insert = anchor + r'''    /* M186_EMPTY_INTERNAL_VOID_CALL_ELISION: do not materialize a pure
       argument solely for a CONFIG-off no-op inline helper. */
    if (returns_void && callee->is_defined && callee->is_internal && callee->is_inline &&
        callee->body_block != MINIC_BLOCK_INVALID) {
        const MinicBlock *callee_body = minic_c0_program_block(
            context->body->program, callee->body_block);
        bool empty_effect = callee_body != NULL && callee_body->statement_count == 0U;
        bool arguments_pure = true;

        if (callee_body != NULL && callee_body->statement_count != 0U) {
            const MinicStatement *first = minic_c0_program_statement(
                context->body->program, callee_body->statements[0]);
            empty_effect = first != NULL && first->kind == MINIC_STATEMENT_RETURN &&
                           first->expression == MINIC_EXPRESSION_INVALID &&
                           first->cleanup_context == first->cleanup_stop_context;
        }
        if (empty_effect) {
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                if (!core_cfg_pure_call_argument(
                        context, expression->value.call.arguments[argument_index], 0U)) {
                    arguments_pure = false;
                    break;
                }
            }
            if (arguments_pure) {
                *value_id = MINIC_CORE_VALUE_INVALID;
                return MINIC_CORE_LOWER_OK;
            }
        }
    }
'''
count = body.count(anchor)
if count != 1:
    raise SystemExit(f"empty-call validation anchor: expected one, found {count}")
body = body.replace(anchor, insert, 1)
text = text[:start] + body + text[end:]
p.write_text(text)
print("MINIC_EMPTY_INTERNAL_VOID_CALL_ELISION_V0=APPLIED pure_arguments=1")
