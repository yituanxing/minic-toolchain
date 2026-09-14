#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

old = '''    if (core_direct_local_read(context, expression_id, &local_id) &&
        core_local_constant_get(context, local_id, &operand_value)) {
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
'''

new = '''    if (core_direct_local_read(context, expression_id, &local_id) &&
        core_local_constant_get(context, local_id, &operand_value)) {
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    /* Linux uses scalar compound literals such as `(int){0}` in constant
     * assertions. Keep the ordinary addressable-object runtime semantics
     * unchanged and recognize only the frontend's single-assignment hidden
     * initializer while answering a compile-time integer query. The semantic
     * conversion pipeline may present either the compound-literal lvalue
     * itself or an explicit lvalue-read wrapper, so accept both shapes. */
    {
        const MinicExpression *compound = expression;

        if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
            compound = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
        }
        if (compound != NULL &&
            compound->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
            minic_type_is_integer(compound->type)) {
            const MinicBlock *initializer_block;
            const MinicLocal *compound_local;

            initializer_block = minic_c0_program_block(
                context->body->program,
                compound->value.compound_literal.initializer_block);
            compound_local = minic_c0_program_local(
                context->body->program,
                compound->value.compound_literal.local_id);
            if (initializer_block != NULL && compound_local != NULL &&
                !compound_local->is_array && !compound_local->is_register_storage &&
                minic_type_is_integer(compound_local->type) &&
                initializer_block->statement_count == 1U) {
                const MinicStatement *initializer;
                const MinicExpression *target;

                initializer = minic_c0_program_statement(
                    context->body->program, initializer_block->statements[0]);
                target = initializer != NULL
                             ? minic_c0_program_expression(
                                   context->body->program,
                                   initializer->target_expression)
                             : NULL;
                if (initializer != NULL && initializer->kind == MINIC_STATEMENT_ASSIGN &&
                    target != NULL && target->kind == MINIC_EXPRESSION_LOCAL &&
                    target->value.local_id == compound->value.compound_literal.local_id &&
                    core_const_eval_integer_with_locals(
                        context, initializer->expression, &operand_value)) {
                    return minic_const_value_convert_integer(
                        context->body->program,
                        context->target,
                        &operand_value,
                        expression->type,
                        value);
                }
            }
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
'''

count = text.count(old)
if count != 1:
    raise SystemExit(f"scalar compound literal anchor: expected one match, found {count}")

p.write_text(text.replace(old, new, 1))
print("MINIC_SCALAR_COMPOUND_LITERAL_CONST_V0=APPLIED")
