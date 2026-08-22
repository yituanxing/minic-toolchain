#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

zero_start_marker = "static bool add_local_array_zero_element("
zero_end_marker = "static bool add_array_object_zero_element("
zero_start = text.find(zero_start_marker)
zero_end = text.find(zero_end_marker, zero_start)
if zero_start < 0 or zero_end < 0:
    raise SystemExit("local array zero helper anchors not found")
text = text[:zero_start] + text[zero_end:]

start_marker = "static bool\nparse_local_array_initializer("
end_marker = "static bool add_zero_assignment_to_lvalue("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("local array initializer anchors not found")

replacement = r'''static bool parse_inferred_runtime_array_initializer(MinicParser *parser,
                                                      MinicLocalId local_id,
                                                      MinicType element_type,
                                                      MinicSourceSpan name_span) {
    MinicArrayInitializerPlan plan;
    bool success;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        return false;
    }
    minic_array_initializer_plan_initialize(&plan, 0U, true);
    success = false;
    if (!minic_parser_advance(parser)) {
        goto done;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId value_id;
        size_t action_id;
        size_t index;

        if (plan.next_index == SIZE_MAX) {
            minic_parser_error(parser, "too many local array initializers");
            goto done;
        }
        if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "out of memory while planning local array initializer");
            goto done;
        }
        index = plan.actions[action_id].first_index;
        if (minic_type_is_record(element_type)) {
            MinicExpressionId base_id;
            MinicExpressionId element_id;

            if (!add_local_lvalue_expression(parser, local_id, name_span, &base_id) ||
                !add_array_object_element_lvalue(
                    parser, base_id, index, parser->current.span, &element_id) ||
                !parse_runtime_record_array_element_initializer(parser, element_id, element_type)) {
                goto done;
            }
        } else if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                   !add_local_array_element_assignment(parser, local_id, index, value_id)) {
            goto done;
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in local array initializer");
            goto done;
        }
    }

    if (minic_array_initializer_plan_element_count(&plan) == 0U) {
        minic_parser_error(parser, "inferred local array initializer must not be empty");
        goto done;
    }
    parser->program->locals[local_id].element_count =
        minic_array_initializer_plan_element_count(&plan);
    success = minic_parser_advance(parser);

done:
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

static bool
parse_local_array_initializer(MinicParser *parser, MinicLocalId local_id, bool infer_count) {
    const MinicLocal *local;
    MinicSourceSpan name_span;
    MinicType element_type;
    size_t declared_count;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || !local->is_array) {
        minic_parser_error(parser, "invalid local array initializer target");
        return false;
    }
    declared_count = local->element_count;
    element_type = local->type;
    name_span = local->name_span;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_char_integer(element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return parse_local_character_array_string_initializer(
            parser, local_id, infer_count, declared_count);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "array initializers are not supported yet");
        return false;
    }
    if (!infer_count) {
        MinicExpressionId base_id;

        if (!add_local_lvalue_expression(parser, local_id, name_span, &base_id)) {
            return false;
        }
        return parse_fixed_runtime_array_initializer(parser, base_id, declared_count);
    }
    return parse_inferred_runtime_array_initializer(parser, local_id, element_type, name_span);
}

'''

text = text[:start] + replacement + text[end:]
path.write_text(text)
