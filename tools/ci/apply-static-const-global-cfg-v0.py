#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
anchor = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
'''
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
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"static-const local evaluator anchor: expected one, found {count}")
p.write_text(text.replace(anchor, insert, 1))
print("MINIC_STATIC_CONST_GLOBAL_CFG_V0=APPLIED")
