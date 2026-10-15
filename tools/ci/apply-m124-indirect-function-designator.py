#!/usr/bin/env python3
"""Stage Core normalization for indirect calls through (*function_pointer)."""

from pathlib import Path

p = Path("src/core/core_lower.c")
s = p.read_text()

old_decl = """    MinicCoreLowerStatus status;\n    MinicType callee_value_type;\n    MinicType function_type;\n"""
new_decl = """    MinicCoreLowerStatus status;\n    MinicExpressionId callee_value_expression_id;\n    MinicType callee_value_type;\n    MinicType function_type;\n"""
if s.count(old_decl) != 1:
    raise SystemExit(f"M124 declaration anchor count={s.count(old_decl)}")
s = s.replace(old_decl, new_decl, 1)

begin = """    callee_expression =\n        minic_c0_program_expression(context->body->program, expression->value.call.callee);\n"""
end = """    signature = minic_c0_program_function_type(\n"""
start = s.find(begin)
finish = s.find(end, start)
if start < 0 or finish < 0:
    raise SystemExit("M124 indirect-call shape region not found")
replacement = r'''    callee_value_expression_id = expression->value.call.callee;
    callee_expression =
        minic_c0_program_expression(context->body->program, callee_value_expression_id);
    /* M124_INDIRECT_FUNCTION_DESIGNATOR: frontend/Sema already accepts both
       pointer-to-function callees and function designators such as `(*fp)`.
       The latter carries function type and its dereference does not perform a
       memory load; its operand is the first-class function-pointer value. Keep
       Core's indirect-call ABI/signature path pointer-valued by normalizing
       only that semantic designator form back to the operand expression. */
    if (callee_expression != NULL &&
        callee_expression->kind == MINIC_EXPRESSION_DEREFERENCE &&
        minic_type_is_function(callee_expression->type)) {
        const MinicExpression *pointer_operand;

        callee_value_expression_id = callee_expression->value.unary.operand;
        pointer_operand = minic_c0_program_expression(
            context->body->program, callee_value_expression_id);
        if (pointer_operand == NULL ||
            !core_scalar_expression_value_type(
                context->body, pointer_operand, &callee_value_type) ||
            !minic_type_pointee(callee_value_type, &function_type) ||
            !minic_type_is_function(function_type) ||
            !minic_type_equal(function_type, callee_expression->type)) {
            (void)fprintf(stderr,
                          "CORE_LOWER_DETAIL marker=M124_INDIRECT_FUNCTION_DESIGNATOR "
                          "function=%s stage=indirect-call reason=dereference-operand-shape\n",
                          context->source_function != NULL ? context->source_function->name : "?");
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else if (callee_expression == NULL ||
               !core_scalar_expression_value_type(
                   context->body, callee_expression, &callee_value_type) ||
               !minic_type_pointee(callee_value_type, &function_type) ||
               !minic_type_is_function(function_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-shape callee_kind=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
s = s[:start] + replacement + s[finish:]

old_lower = """    status = lower_expression(context, expression->value.call.callee, &callee_value);\n"""
new_lower = """    status = lower_expression(context, callee_value_expression_id, &callee_value);\n"""
if s.count(old_lower) != 1:
    raise SystemExit(f"M124 callee-lower anchor count={s.count(old_lower)}")
s = s.replace(old_lower, new_lower, 1)

p.write_text(s)
print("staged M124 indirect function-designator normalization")
