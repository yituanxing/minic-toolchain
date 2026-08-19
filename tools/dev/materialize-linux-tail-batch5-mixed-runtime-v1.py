#!/usr/bin/env python3
from pathlib import Path

p = Path('src/frontend/parser_statement.c')
text = p.read_text()

# This script runs after materialize-linux-tail-batch5-v1.py, so the positional
# owner already has `field_limit` and admits complete unions.
pos_marker = 'static bool parse_positional_runtime_record_initializer('
pos_start = text.index(pos_marker)

# Keep zero-fill of untouched fields in one helper. This is needed when a list
# begins positionally and then switches to a designator: prior positional
# assignments must remain intact, while the not-yet-touched suffix gets normal
# aggregate zero initialization before designated overwrites.
zero_helper = r'''static bool zero_runtime_record_fields_from(MinicParser *parser,
                                            MinicExpressionId target_id,
                                            MinicRecordId record_id,
                                            size_t *field_index,
                                            size_t field_limit,
                                            MinicSourceSpan initializer_span) {
    const MinicRecord *record;

    if (parser == NULL || field_index == NULL) {
        return false;
    }
    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL || !record->is_complete || field_limit > record->field_count) {
        return false;
    }
    while (*field_index < field_limit) {
        MinicExpressionId member_id;

        if (record->fields[*field_index].is_flexible_array) {
            *field_index += 1U;
            continue;
        }
        if (!add_record_field_lvalue(parser,
                                     target_id,
                                     record_id,
                                     *field_index,
                                     initializer_span,
                                     &member_id)) {
            return false;
        }
        if (record->fields[*field_index].is_array) {
            if (!add_array_object_zero_elements(parser,
                                                member_id,
                                                record->fields[*field_index].element_count,
                                                initializer_span)) {
                return false;
            }
        } else if (minic_type_is_record(record->fields[*field_index].type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
        *field_index += 1U;
    }
    return true;
}

static bool parse_runtime_record_designated_tail(MinicParser *parser,
                                                 MinicExpressionId target_id);

'''
text = text[:pos_start] + zero_helper + text[pos_start:]
pos_start = text.index(pos_marker)
pos_end = text.index('\nstatic bool parse_runtime_record_designator_target(', pos_start)
pos_segment = text[pos_start:pos_end]

old_comma = r'''        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {'''
new_comma = r'''        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
            if (parser->current.kind == MINIC_TOKEN_DOT) {
                initializer_span.end = parser->current.span.end;
                if (!zero_runtime_record_fields_from(parser,
                                                     target_id,
                                                     record_id,
                                                     &field_index,
                                                     field_limit,
                                                     initializer_span)) {
                    return false;
                }
                return parse_runtime_record_designated_tail(parser, target_id);
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {'''
if pos_segment.count(old_comma) != 1:
    raise SystemExit(f'positional comma branch count={pos_segment.count(old_comma)}')
pos_segment = pos_segment.replace(old_comma, new_comma, 1)

loop_start = pos_segment.index('    while (field_index < field_limit) {')
return_marker = '    return minic_parser_advance(parser);\n}'
loop_end = pos_segment.index(return_marker, loop_start)
replacement = r'''    if (!zero_runtime_record_fields_from(parser,
                                         target_id,
                                         record_id,
                                         &field_index,
                                         field_limit,
                                         initializer_span)) {
        return false;
    }
'''
pos_segment = pos_segment[:loop_start] + replacement + pos_segment[loop_end:]
text = text[:pos_start] + pos_segment + text[pos_end:]

# Extract the existing designated-list loop into one helper, so both a list
# that starts designated and a positional list that switches at a comma use the
# same parser/assignment owner.
target_marker = 'static bool parse_runtime_record_designator_target('
target_start = text.index(target_marker)
public_marker = 'bool minic_parser_parse_runtime_record_initializer('
public_start = text.index(public_marker, target_start)
helper = r'''static bool parse_runtime_record_designated_tail(MinicParser *parser,
                                                 MinicExpressionId target_id) {
    for (;;) {
        MinicExpressionId member_id;
        const MinicExpression *member;
        MinicArrayObjectInfo member_array;
        MinicType member_type;
        bool member_is_array;

        if (!parse_runtime_record_designator_target(parser, target_id, &member_id) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_EQUAL, "expected '=' after record designator")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "expected designated record initializer");
            }
            return false;
        }
        member = minic_c0_program_expression(parser->program, member_id);
        if (member == NULL || member->value_category != MINIC_VALUE_LVALUE) {
            minic_parser_error(parser, "record designated initializer type mismatch");
            return false;
        }
        member_type = member->type;
        member_is_array =
            minic_c0_expression_array_object_info(parser->program, member, &member_array);
        if (parser->current.kind == MINIC_TOKEN_LBRACE && member_is_array) {
            if (member_array.is_incomplete || member_array.is_zero_length ||
                !parse_fixed_runtime_array_initializer(
                    parser, member_id, member_array.element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "unsupported designated record array initializer");
                }
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_LBRACE &&
                   minic_type_is_record(member_type)) {
            if (!minic_parser_parse_runtime_record_initializer(parser, member_id)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (member_is_array) {
                minic_parser_error(parser,
                                   "runtime record array field initializer requires braces");
                return false;
            }
            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !add_runtime_record_member_assignment(parser, member_id, value_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "record designated initializer type mismatch");
                }
                return false;
            }
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                return minic_parser_advance(parser);
            }
            if (parser->current.kind != MINIC_TOKEN_DOT) {
                minic_parser_error(
                    parser, "positional initializer after a runtime record designator is unsupported");
                return false;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in designated record initializer");
            return false;
        }
        return minic_parser_advance(parser);
    }
}

'''
text = text[:public_start] + helper + text[public_start:]

public_start = text.index(public_marker)
loop_start = text.index('    for (;;) {', public_start)
function_end = text.index('\n}\n\ntypedef struct MinicLocalObjectAttributes', loop_start)
text = text[:loop_start] + '    return parse_runtime_record_designated_tail(parser, target_id);' + text[function_end:]

# Zeroing a union object means zero-initializing its first member, not emitting
# assignments for every overlapping member.
zero_fn = text.index('static bool add_zero_initialized_record_lvalue(')
zero_end = text.index('\nstatic bool add_record_copy_assignments(', zero_fn)
zero_segment = text[zero_fn:zero_end]
if 'size_t field_limit;' not in zero_segment:
    zero_segment = zero_segment.replace('    size_t field_index;\n',
                                        '    size_t field_index;\n    size_t field_limit;\n', 1)
zero_segment = zero_segment.replace(
    '    record = minic_c0_program_record(parser->program, record_id);\n'
    '    if (record == NULL || !record->is_complete) {',
    '    record = minic_c0_program_record(parser->program, record_id);\n'
    '    if (record == NULL || !record->is_complete) {',
    1)
needle = '        return false;\n    }\n\n    (void)memset(&address, 0, sizeof(address));'
if zero_segment.count(needle) != 1:
    raise SystemExit('cannot locate zero-record completeness tail')
zero_segment = zero_segment.replace(
    needle,
    '        return false;\n    }\n'
    '    field_limit = record->is_union ? (record->field_count == 0U ? 0U : 1U)\n'
    '                                   : record->field_count;\n\n'
    '    (void)memset(&address, 0, sizeof(address));',
    1)
if zero_segment.count('field_index < record->field_count') != 1:
    raise SystemExit('cannot locate zero-record field loop')
zero_segment = zero_segment.replace('field_index < record->field_count',
                                    'field_index < field_limit', 1)
text = text[:zero_fn] + zero_segment + text[zero_end:]

p.write_text(text)
print('materialized mixed positional/designated runtime record initialization')
