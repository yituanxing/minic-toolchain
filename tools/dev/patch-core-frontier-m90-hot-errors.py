#!/usr/bin/env python3
# Advance two hot Core error cohorts: legacy array address-of and failure-only scalar-call tracing.

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M90_LEGACY_ARRAY_ADDRESS_OF"
TRACE_MARKER = "M90_HOT_ERROR_DETAIL"


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M90 {name} anchor count={count}")
    return text.replace(old, new, 1)


def replace_in_region(text: str, begin: str, end: str, old: str, new: str, name: str) -> str:
    begin_index = text.find(begin)
    if begin_index < 0:
        raise SystemExit(f"M90 {name} region begin missing")
    end_index = text.find(end, begin_index + len(begin))
    if end_index < 0:
        raise SystemExit(f"M90 {name} region end missing")
    region = text[begin_index:end_index]
    count = region.count(old)
    if count != 1:
        raise SystemExit(f"M90 {name} region anchor count={count}")
    region = region.replace(old, new, 1)
    return text[:begin_index] + region + text[end_index:]


def patch_legacy_array_address(text: str) -> str:
    if MARKER in text:
        print("M90 legacy array address-of already applied")
        return text

    old = '''        if (*value_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[*value_id].type, expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return MINIC_CORE_LOWER_OK;
'''
    new = '''        if (*value_id >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_equal(context->function->values[*value_id].type, expression->type)) {
            return MINIC_CORE_LOWER_OK;
        }
        /* M90_LEGACY_ARRAY_ADDRESS_OF: legacy local/member arrays keep their
           element type plus array-object metadata. lower_address() therefore
           yields the element-address representation, while C `&array` carries
           pointer-to-array type. The address bits are identical; preserve the
           semantic pointer type with Core's target-neutral scalar bitcast. */
        {
            const MinicExpression *addressed = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            MinicArrayObjectInfo array_info;

            (void)memset(&array_info, 0, sizeof(array_info));
            if (addressed != NULL &&
                minic_c0_expression_array_object_info(
                    context->body->program, addressed, &array_info) &&
                minic_type_is_pointer(context->function->values[*value_id].type) &&
                minic_type_is_pointer(expression->type)) {
                return append_scalar_bitcast(
                    context, expression->span, expression->type, *value_id, value_id);
            }
        }
        return MINIC_CORE_LOWER_ERROR;
'''
    return replace_in_region(
        text,
        "    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {",
        "    if (expression->kind == MINIC_EXPRESSION_CALL) {",
        old,
        new,
        "legacy-array-address-of",
    )


def patch_failure_detail(text: str) -> str:
    if TRACE_MARKER in text:
        print("M90 hot-error detail already applied")
        return text

    # These diagnostics intentionally omit `span=` and CORE_FAST_TRACE so they
    # cannot become corpus_replay's first-frontier locator. They only execute on
    # a lowering failure and identify which scalar-call/binary stage failed.
    begin = "static MinicCoreLowerStatus lower_integer_binary_operands("
    end = "/* M80_ADDRESS_BACKED_RECORD_COPY:"
    replacements = [
        (
            '''    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
            '''    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-lower status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
''',
            "integer-left-lower",
        ),
        (
            '''    status = append_integer_conversion(
        context, left_expression->span, result_type, left_source, &left_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
            '''    status = append_integer_conversion(
        context, left_expression->span, result_type, left_source, &left_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-convert status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
''',
            "integer-left-convert",
        ),
        (
            '''    status = spill_scalar_value(
        context, left_expression->span, result_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
            '''    status = spill_scalar_value(
        context, left_expression->span, result_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-spill status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
''',
            "integer-left-spill",
        ),
        (
            '''    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
            '''    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=right-lower status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
''',
            "integer-right-lower",
        ),
        (
            '''    status = append_integer_conversion(
        context, right_expression->span, result_type, right_source, &right_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
            '''    status = append_integer_conversion(
        context, right_expression->span, result_type, right_source, &right_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=right-convert status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
''',
            "integer-right-convert",
        ),
        (
            '''    status =
        reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
            '''    status =
        reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-reload status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
''',
            "integer-left-reload",
        ),
    ]
    for old, new, name in replacements:
        text = replace_in_region(text, begin, end, old, new, name)

    call_begin = "static MinicCoreLowerStatus lower_direct_call("
    call_end = "/* M86_DIRECT_RECORD_CALL_RESULT:"
    old = '''        status = lower_scalar_assignment_value(
            context,
            callee->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
'''
    new = '''        status = lower_scalar_assignment_value(
            context,
            callee->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=direct-call callee=%s arg=%zu reason=argument-lower status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          callee_name, argument_index, (int)status);
            free(arguments);
            return status;
        }
'''
    text = replace_in_region(text, call_begin, call_end, old, new, "call-argument-lower")

    old = '''        status = spill_scalar_value(context,
                                    expression->span,
                                    callee->parameter_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
'''
    new = '''        status = spill_scalar_value(context,
                                    expression->span,
                                    callee->parameter_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=direct-call callee=%s arg=%zu reason=argument-spill status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          callee_name, argument_index, (int)status);
            free(arguments);
            return status;
        }
'''
    text = replace_in_region(text, call_begin, call_end, old, new, "call-argument-spill")
    return text


def main() -> int:
    text = PATH.read_text()
    text = patch_legacy_array_address(text)
    text = patch_failure_detail(text)
    PATH.write_text(text)
    print("M90 legacy array address-of + failure-only hot-error detail applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
