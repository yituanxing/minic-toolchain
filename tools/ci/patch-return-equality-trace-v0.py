#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

def replace_once(old: str, new: str) -> None:
    global text
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"trace anchor count={n}: {old[:100]!r}")
    text = text.replace(old, new, 1)

replace_once(
'''    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    return append_integer_conversion(
        context, expression->span, result_type, source_value, value_id);
}
''',
'''    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        if (getenv("MINIC_RETURN_EQUALITY_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "RETURN_EQ integer-assignment fn=%s expr_kind=%d target_base=%d expr_base=%d status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          expression != NULL ? (int)expression->kind : -1,
                          (int)target_type.base_kind,
                          expression != NULL ? (int)expression->type.base_kind : -1,
                          (int)status);
        }
        return status;
    }
    status = append_integer_conversion(
        context, expression->span, result_type, source_value, value_id);
    if (status != MINIC_CORE_LOWER_OK && getenv("MINIC_RETURN_EQUALITY_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "RETURN_EQ integer-assignment-convert fn=%s expr_kind=%d result_base=%d status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      expression != NULL ? (int)expression->kind : -1,
                      (int)result_type.base_kind,
                      (int)status);
    }
    return status;
}
''')

replace_once(
'''    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (left_source >= context->function->value_count ||
''',
'''    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        if (getenv("MINIC_RETURN_EQUALITY_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "RETURN_EQ equality-left fn=%s left_kind=%d left_base=%d status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          left_expression != NULL ? (int)left_expression->kind : -1,
                          (int)left_type.base_kind,
                          (int)status);
        }
        return status;
    }
    if (left_source >= context->function->value_count ||
''')

replace_once(
'''    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (right_source >= context->function->value_count ||
''',
'''    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        if (getenv("MINIC_RETURN_EQUALITY_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "RETURN_EQ equality-right fn=%s right_kind=%d right_base=%d status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          right_expression != NULL ? (int)right_expression->kind : -1,
                          (int)right_type.base_kind,
                          (int)status);
        }
        return status;
    }
    if (right_source >= context->function->value_count ||
''')

replace_once(
'''            status = lower_scalar_assignment_value(context,
                                                   context->source_function->return_type,
                                                   statement->expression,
                                                   &terminator.return_value);
''',
'''            status = lower_scalar_assignment_value(context,
                                                   context->source_function->return_type,
                                                   statement->expression,
                                                   &terminator.return_value);
            if (status != MINIC_CORE_LOWER_OK && getenv("MINIC_RETURN_EQUALITY_TRACE") != NULL) {
                const MinicExpression *return_expression = minic_c0_program_expression(
                    context->body->program, statement->expression);
                (void)fprintf(stderr,
                              "RETURN_EQ return fn=%s expr_kind=%d return_base=%d expr_base=%d status=%d\\n",
                              context->source_function->name,
                              return_expression != NULL ? (int)return_expression->kind : -1,
                              (int)context->source_function->return_type.base_kind,
                              return_expression != NULL ? (int)return_expression->type.base_kind : -1,
                              (int)status);
            }
''')

p.write_text(text)
print("MINIC_RETURN_EQUALITY_TRACE_PATCH=APPLIED")
