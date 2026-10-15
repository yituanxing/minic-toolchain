#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
# Match the actual definition, not a forward declaration added by later CFG
# closure passes.
fn = '''static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,
                                                MinicExpressionId expression_id,
                                                MinicConstValue *value) {
'''
start = text.find(fn)
if start < 0:
    raise SystemExit("static-const: local integer evaluator definition missing")
anchor = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
'''
pos = text.find(anchor, start)
if pos < 0:
    raise SystemExit("static-const: evaluator expression-validation anchor missing")
next_fn = text.find("\nstatic ", start + len(fn))
if next_fn >= 0 and pos >= next_fn:
    raise SystemExit("static-const: anchor escaped local evaluator definition")
insert = anchor + r'''    {
        const MinicExpression *object_expression = expression;
        if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            if (operand != NULL) {
                object_expression = operand;
            }
        }
        if (object_expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
            const MinicGlobalObject *object = minic_c0_program_global_object(
                context->body->program, object_expression->value.global_object_id);
            MinicType object_type;
            if (object != NULL && object->is_internal && object->is_read_only &&
                !object->is_extern && object->relocation_count == 0U &&
                minic_type_unqualified(object->type, &object_type) &&
                minic_type_is_integer(object_type) &&
                (object->initializer_count == 1U || object->is_zero_initialized)) {
                MinicConstValue source;
                source.type = object_type;
                source.bits = object->initializer_count == 1U
                                  ? object->initializer_values[0]
                                  : 0U;
                if (minic_const_value_convert_integer(context->body->program,
                                                      context->target,
                                                      &source,
                                                      expression->type,
                                                      value)) {
                    return true;
                }
            }
        }
    }
'''
text = text[:pos] + insert + text[pos + len(anchor):]
p.write_text(text)
print("MINIC_STATIC_CONST_GLOBAL_CFG_V0=APPLIED")
