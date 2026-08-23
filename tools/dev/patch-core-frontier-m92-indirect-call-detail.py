#!/usr/bin/env python3
# M92: failure-only diagnostics for the indirect-call hot frontier.

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M92_INDIRECT_CALL_HOT_DETAIL"


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M92 {name} anchor count={count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M92 indirect-call detail already applied")
        return 0

    shape = '''    callee_expression =
        minic_c0_program_expression(context->body->program, expression->value.call.callee);
    if (callee_expression == NULL ||
        !core_scalar_expression_value_type(context->body, callee_expression, &callee_value_type) ||
        !minic_type_pointee(callee_value_type, &function_type) ||
        !minic_type_is_function(function_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    shape_new = '''    callee_expression =
        minic_c0_program_expression(context->body->program, expression->value.call.callee);
    if (callee_expression == NULL ||
        !core_scalar_expression_value_type(context->body, callee_expression, &callee_value_type) ||
        !minic_type_pointee(callee_value_type, &function_type) ||
        !minic_type_is_function(function_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-shape callee_kind=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    text = replace_once(text, shape, shape_new, "callee-shape")

    signature = '''    if (signature == NULL || signature->is_variadic ||
        expression->value.call.argument_count != signature->parameter_count ||
        !minic_type_equal(expression->type, signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    signature_new = '''    if (signature == NULL || signature->is_variadic ||
        expression->value.call.argument_count != signature->parameter_count ||
        !minic_type_equal(expression->type, signature->return_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=signature signature=%d variadic=%d argc=%zu expected=%zu return_match=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      signature != NULL ? 1 : 0,
                      signature != NULL && signature->is_variadic ? 1 : 0,
                      expression->value.call.argument_count,
                      signature != NULL ? signature->parameter_count : 0U,
                      signature != NULL && minic_type_equal(expression->type, signature->return_type) ? 1 : 0);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    text = replace_once(text, signature, signature_new, "signature")

    callee_lower = '''    status = lower_expression(context, expression->value.call.callee, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
    callee_lower_new = '''    status = lower_expression(context, expression->value.call.callee, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-lower status=%d callee_kind=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status,
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        return status;
    }
'''
    text = replace_once(text, callee_lower, callee_lower_new, "callee-lower")

    value_type = '''    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type,
                          callee_value_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
'''
    value_type_new = '''    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type,
                          callee_value_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-value-type value=%u count=%zu\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (unsigned int)callee_value,
                      context->function->value_count);
        return MINIC_CORE_LOWER_ERROR;
    }
'''
    text = replace_once(text, value_type, value_type_new, "callee-value-type")

    PATH.write_text(text)
    print("M92 indirect-call hot detail applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
