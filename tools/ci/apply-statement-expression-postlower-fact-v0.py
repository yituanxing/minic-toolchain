#!/usr/bin/env python3
from pathlib import Path

# GNU statement expressions are lowered before an enclosing scalar assignment
# records its local constant fact.  After lowering, control-flow pruning may
# have established a precise fact for the statement expression's final local
# even when the conservative pre-lowering CFG interpreter cannot model the
# whole block (for example an if/else chain selected entirely by sizeof).
# Consume that already-proven result fact before falling back to reinterpreting
# the whole statement expression.  This is CFG-fact propagation only; runtime
# lowering and emitted values are unchanged.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M191_STATEMENT_EXPRESSION_POSTLOWER_FACT"
if marker in text:
    print("MINIC_STATEMENT_EXPRESSION_POSTLOWER_FACT_V0=ALREADY")
else:
    old = '''        null_pointer_known = core_expression_known_null_pointer(context, source_id);
        core_local_constant_invalidate(context, target->value.local_id);
        fact_known = core_const_eval_integer_with_locals(context, source_id, &stored_constant);
        if (fact_known) {
'''
    new = '''        null_pointer_known = core_expression_known_null_pointer(context, source_id);
        /* M191_STATEMENT_EXPRESSION_POSTLOWER_FACT: lower_scalar_assignment_value()
           has already executed a GNU statement-expression block at this point.
           If its final expression is now a proven integer local fact, consume
           that fact directly instead of asking the intentionally narrow
           pre-lowering statement-expression interpreter to replay control flow. */
        fact_known = false;
        if (source != NULL && source->kind == MINIC_EXPRESSION_STATEMENT &&
            source->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
            MinicConstValue statement_result_constant;
            MinicConstValue converted_statement_result;
            if (core_const_eval_integer_with_locals(
                    context,
                    source->value.statement_expression.result,
                    &statement_result_constant) &&
                minic_const_value_convert_integer(context->body->program,
                                                  context->target,
                                                  &statement_result_constant,
                                                  stored_type,
                                                  &converted_statement_result)) {
                stored_constant = converted_statement_result;
                fact_known = true;
            }
        }
        core_local_constant_invalidate(context, target->value.local_id);
        if (!fact_known) {
            fact_known = core_const_eval_integer_with_locals(
                context, source_id, &stored_constant);
        }
        if (fact_known) {
'''
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"statement-expression postlower fact anchor: expected one, found {count}")
    text = text.replace(old, new, 1)
    p.write_text(text)
    print("MINIC_STATEMENT_EXPRESSION_POSTLOWER_FACT_V0=APPLIED")
