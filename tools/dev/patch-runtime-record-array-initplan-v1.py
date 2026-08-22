#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
start_marker = "static bool parse_fixed_runtime_record_array_initializer_legacy("
end_marker = "static bool grow_runtime_array_action_values("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("runtime record-array initializer anchors not found")

replacement = r'''static bool parse_fixed_runtime_record_array_initializer(MinicParser *parser,
                                                         MinicExpressionId base_id,
                                                         size_t element_count) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;
    MinicArrayInitializerPlan plan;
    MinicSourceSpan initializer_span;
    bool success;

    if (parser == NULL || element_count == 0U || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser,
                               "fixed runtime record array initializer requires a nonempty record array");
        }
        return false;
    }
    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info) ||
        !minic_type_is_record(array_info.element_type)) {
        minic_parser_error(parser,
                           "fixed runtime record array initializer requires a nonempty record array");
        return false;
    }

    minic_array_initializer_plan_initialize(&plan, element_count, false);
    initializer_span.begin = parser->current.span.begin;
    success = false;
    if (!minic_parser_advance(parser)) {
        goto done;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;
        size_t target_index;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            MinicSourceSpan zero_span;
            size_t first;
            size_t last;
            size_t previous_next;
            size_t index;

            previous_next = plan.next_index;
            if (!parse_runtime_array_designator(
                    parser, element_count, plan.next_index, &first, &last)) {
                goto done;
            }
            zero_span = parser->current.span;
            if (first != last) {
                minic_parser_error(parser, "record array range designators require one element");
                goto done;
            }
            if (!minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                minic_parser_error(parser, "cannot plan runtime record array initializer");
                goto done;
            }
            for (index = previous_next; index < first; ++index) {
                if (!add_array_object_zero_element(parser, base_id, index, zero_span)) {
                    goto done;
                }
            }
            target_index = plan.actions[action_id].first_index;
        } else {
            if (plan.next_index >= element_count) {
                minic_parser_error(parser, "too many runtime array initializer elements");
                goto done;
            }
            if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
                minic_parser_error(parser, "cannot plan runtime record array initializer");
                goto done;
            }
            target_index = plan.actions[action_id].first_index;
        }

        {
            MinicExpressionId element_id;

            if (!add_array_object_element_lvalue(
                    parser, base_id, target_index, parser->current.span, &element_id) ||
                !parse_runtime_record_array_element_initializer(
                    parser, element_id, array_info.element_type)) {
                goto done;
            }
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in runtime array initializer");
            goto done;
        }
    }

    initializer_span.end = parser->current.span.end;
    {
        size_t index;

        for (index = plan.next_index; index < element_count; ++index) {
            if (!add_array_object_zero_element(parser, base_id, index, initializer_span)) {
                goto done;
            }
        }
    }
    success = minic_parser_advance(parser);

done:
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

'''

text = text[:start] + replacement + text[end:]
old_call = "return parse_fixed_runtime_record_array_initializer_legacy(parser, base_id, element_count);"
new_call = "return parse_fixed_runtime_record_array_initializer(parser, base_id, element_count);"
if text.count(old_call) != 1:
    raise SystemExit("legacy runtime record-array call anchor not unique")
text = text.replace(old_call, new_call)
path.write_text(text)
