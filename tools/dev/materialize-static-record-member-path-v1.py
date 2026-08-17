from pathlib import Path

path = Path('src/frontend/parser_global.c')
text = path.read_text()
start_marker = '''static bool parse_static_record_constant(MinicParser *parser,
'''
end_marker = '''bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
'''
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0 or end <= start:
    raise SystemExit('static record initializer function boundary changed')

replacement = r'''typedef struct MinicStaticRecordDesignator {
    size_t field_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
} MinicStaticRecordDesignator;

static bool parse_static_record_designator_path(MinicParser *parser,
                                                const MinicRecord *record,
                                                MinicStaticRecordDesignator *designator) {
    const MinicRecord *current_record;

    if (parser == NULL || record == NULL || designator == NULL ||
        parser->current.kind != MINIC_TOKEN_DOT) {
        return false;
    }
    (void)memset(designator, 0, sizeof(*designator));
    current_record = record;
    while (parser->current.kind == MINIC_TOKEN_DOT) {
        MinicRecordFieldPath field_path;
        MinicSourceSpan field_span;
        const MinicRecordField *field;
        size_t field_index;

        if (designator->depth >= MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
            minic_parser_error(parser, "static record designator path exceeds implementation limit");
            return false;
        }
        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected member name after '.' in initializer");
            return false;
        }
        field_span = parser->current.span;
        if (!minic_parser_find_record_field_path(parser, current_record, field_span, &field_path) ||
            !field_path.found || field_path.ambiguous || field_path.depth != 1U) {
            minic_parser_error(parser,
                               "static record designator requires a direct unambiguous member at each path segment");
            return false;
        }
        field_index = field_path.field_indices[0];
        if (current_record->is_union && field_index != 0U) {
            minic_parser_error(parser,
                               "nested static union designator requires the representable first member");
            return false;
        }
        designator->field_indices[designator->depth++] = field_index;
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || !minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_record(field->type)) {
            minic_parser_error(parser,
                               "intermediate static record designator member must be a scalar record");
            return false;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            minic_parser_error(parser,
                               "static record designator path requires complete record members");
            return false;
        }
    }
    return designator->depth != 0U &&
           minic_parser_expect(parser,
                               MINIC_TOKEN_EQUAL,
                               "expected '=' after static record designator");
}

static bool static_record_designator_scalar_slot(const MinicC0Program *program,
                                                 const MinicRecord *record,
                                                 const MinicStaticRecordDesignator *designator,
                                                 size_t *slot_index,
                                                 MinicType *slot_type) {
    const MinicRecord *current_record;
    size_t depth;
    size_t total;

    if (program == NULL || record == NULL || designator == NULL || slot_index == NULL ||
        slot_type == NULL || designator->depth == 0U) {
        return false;
    }
    current_record = record;
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t relative;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array ||
            !minic_c0_global_record_field_initializer_slot(
                program, current_record, field_index, &relative) || total > SIZE_MAX - relative) {
            return false;
        }
        total += relative;
        if (depth + 1U == designator->depth) {
            if (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type)) {
                return false;
            }
            *slot_index = total;
            *slot_type = field->type;
            return true;
        }
        if (!minic_type_is_record(field->type)) {
            return false;
        }
        current_record = minic_c0_program_record(program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return false;
        }
    }
    return false;
}

static bool append_static_record_designator_value(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  const size_t *field_indices,
                                                  size_t depth) {
    const MinicRecordField *field;
    size_t field_index;
    size_t field_limit;
    size_t selected_index;

    if (parser == NULL || record == NULL || !record->is_complete || field_indices == NULL ||
        depth == 0U) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    selected_index = field_indices[0];
    if (selected_index >= field_limit) {
        return false;
    }
    for (field_index = 0U; field_index < selected_index; ++field_index) {
        if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
            minic_parser_error(parser, "cannot zero-fill static record designator prefix");
            return false;
        }
    }
    field = &record->fields[selected_index];
    if (field->element_count == 0U || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported static record designator field");
        return false;
    }
    if (depth == 1U) {
        if (field->element_count == 1U && !field->is_array) {
            if (!minic_parser_parse_static_storage_initializer_value(
                    parser, object_id, field->type)) {
                return false;
            }
        } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                   parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            if (!minic_parser_add_bounded_string_literal_initializer(
                    parser, object_id, field->element_count)) {
                return false;
            }
        } else if (!parse_static_forward_array_initializer(
                       parser, object_id, field->type, field->element_count, false, NULL)) {
            return false;
        }
    } else {
        const MinicRecord *nested_record;

        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(parser,
                               "nested static record designator path requires scalar record members");
            return false;
        }
        nested_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (nested_record == NULL ||
            !append_static_record_designator_value(
                parser, object_id, nested_record, field_indices + 1U, depth - 1U)) {
            return false;
        }
    }
    for (field_index = selected_index + 1U; field_index < field_limit; ++field_index) {
        if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
            minic_parser_error(parser, "cannot zero-fill static record designator suffix");
            return false;
        }
    }
    return true;
}

static bool parse_static_record_constant(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const MinicRecord *record) {
    size_t field_index;
    size_t field_limit;
    size_t materialized_field_limit;
    size_t record_base_slot;

    if (record == NULL || !record->is_complete ||
        object_id >= parser->program->global_object_count ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    materialized_field_limit = 0U;
    record_base_slot = parser->program->global_objects[object_id].initializer_count;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicStaticRecordDesignator designator;
        const MinicRecordField *field;
        bool has_designator;
        bool overwrite_materialized_field;

        has_designator = false;
        (void)memset(&designator, 0, sizeof(designator));
        if (parser->current.kind == MINIC_TOKEN_DOT) {
            size_t designator_index;

            if (!parse_static_record_designator_path(parser, record, &designator)) {
                return false;
            }
            designator_index = designator.field_indices[0];
            while (materialized_field_limit < designator_index) {
                if (!append_static_field_zeros(
                        parser, object_id, &record->fields[materialized_field_limit])) {
                    minic_parser_error(parser,
                                       "cannot zero-fill skipped static record designator fields");
                    return false;
                }
                materialized_field_limit += 1U;
            }
            field_index = designator_index;
            has_designator = true;
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many nested static record initializers");
            return false;
        }
        field = &record->fields[field_index];
        if (field->element_count == 0U || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported nested static record field");
            return false;
        }
        overwrite_materialized_field = field_index < materialized_field_limit;
        if (has_designator && designator.depth > 1U) {
            if (overwrite_materialized_field) {
                size_t relative_slot;
                size_t slot_index;
                MinicType slot_type;

                if (!static_record_designator_scalar_slot(parser->program,
                                                          record,
                                                          &designator,
                                                          &relative_slot,
                                                          &slot_type) ||
                    record_base_slot > SIZE_MAX - relative_slot) {
                    minic_parser_error(
                        parser,
                        "backward nested static record designator currently requires a scalar leaf");
                    return false;
                }
                slot_index = record_base_slot + relative_slot;
                if (!parse_static_scalar_constant_at(
                        parser, object_id, slot_type, true, slot_index)) {
                    return false;
                }
            } else {
                const MinicRecord *nested_record;

                if (field_index != materialized_field_limit || field->element_count != 1U ||
                    field->is_array || field->is_bit_field || !minic_type_is_record(field->type)) {
                    minic_parser_error(parser,
                                       "forward nested static record designator requires a scalar record field");
                    return false;
                }
                nested_record = minic_c0_program_record(parser->program, field->type.record_id);
                if (nested_record == NULL ||
                    !append_static_record_designator_value(parser,
                                                           object_id,
                                                           nested_record,
                                                           designator.field_indices + 1U,
                                                           designator.depth - 1U)) {
                    return false;
                }
                materialized_field_limit += 1U;
            }
        } else if (overwrite_materialized_field) {
            size_t relative_slot;
            size_t slot_index;

            if (field->element_count != 1U ||
                (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type)) ||
                !minic_c0_global_record_field_initializer_slot(
                    parser->program, record, field_index, &relative_slot) ||
                record_base_slot > SIZE_MAX - relative_slot) {
                minic_parser_error(
                    parser,
                    "backward static record designator currently requires a direct scalar field");
                return false;
            }
            slot_index = record_base_slot + relative_slot;
            if (!parse_static_scalar_constant_at(
                    parser, object_id, field->type, true, slot_index)) {
                return false;
            }
        } else {
            if (field_index != materialized_field_limit) {
                minic_parser_error(parser, "internal error: invalid static record materialization");
                return false;
            }
            if (field->element_count == 1U && !field->is_array) {
                if (!minic_parser_parse_static_storage_initializer_value(
                        parser, object_id, field->type)) {
                    return false;
                }
            } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                       parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
                if (!minic_parser_add_bounded_string_literal_initializer(
                        parser, object_id, field->element_count)) {
                    return false;
                }
            } else if (!parse_static_forward_array_initializer(
                           parser, object_id, field->type, field->element_count, false, NULL)) {
                return false;
            }
            materialized_field_limit += 1U;
        }
        field_index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in nested static record initializer");
            return false;
        }
    }
    while (materialized_field_limit < field_limit) {
        if (!append_static_field_zeros(
                parser, object_id, &record->fields[materialized_field_limit])) {
            minic_parser_error(parser, "cannot zero-fill nested static record initializer");
            return false;
        }
        materialized_field_limit += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer");
}

'''
path.write_text(text[:start] + replacement + text[end:])
