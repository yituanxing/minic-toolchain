from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, got {count}')
    p.write_text(text.replace(old, new, 1))

# String owner: decode exactly as normal bounded strings, but write only into
# pre-existing implicit-zero aggregate slots.
replace_once(
    'src/frontend/parser_internal.h',
    '''bool minic_parser_add_bounded_string_literal_initializer(MinicParser *parser,\n                                                         MinicGlobalObjectId object_id,\n                                                         size_t element_capacity);\n''',
    '''bool minic_parser_add_bounded_string_literal_initializer(MinicParser *parser,\n                                                         MinicGlobalObjectId object_id,\n                                                         size_t element_capacity);\nbool minic_parser_replace_zero_bounded_string_literal_initializer(\n    MinicParser *parser,\n    MinicGlobalObjectId object_id,\n    size_t first_slot,\n    size_t element_capacity);\n''',
    'bounded string overwrite declaration')

string_path = Path('src/frontend/parser_string.c')
text = string_path.read_text()
anchor = '''bool minic_parser_get_predefined_function_name_object(MinicParser *parser,\n'''
helper = r'''static bool replace_zero_string_payload(MinicParser *parser,
                                        MinicSourceSpan span,
                                        MinicTokenKind kind,
                                        MinicGlobalObjectId object_id,
                                        size_t *slot_index,
                                        size_t slot_limit) {
    size_t cursor;
    size_t end;

    if (slot_index == NULL ||
        !string_literal_payload_bounds(parser, span, kind, &cursor, &end)) {
        return false;
    }
    while (cursor < end) {
        int value;

        if (*slot_index >= slot_limit) {
            return false;
        }
        if (parser->source[cursor] == '\\') {
            cursor += 1U;
            if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                minic_parser_error(parser, "unsupported string escape");
                return false;
            }
        } else {
            value = (int)(unsigned char)parser->source[cursor];
            cursor += 1U;
        }
        if (!minic_c0_global_object_replace_zero_initializer_bits(
                parser->program,
                object_id,
                *slot_index,
                (uint64_t)(int64_t)value)) {
            minic_parser_error(parser,
                               "backward string initializer can only replace implicit zero slots");
            return false;
        }
        *slot_index += 1U;
    }
    return true;
}

bool minic_parser_replace_zero_bounded_string_literal_initializer(
    MinicParser *parser,
    MinicGlobalObjectId object_id,
    size_t first_slot,
    size_t element_capacity) {
    MinicParser probe;
    size_t decoded_length;
    size_t total_length;
    size_t slot_index;
    size_t slot_limit;

    if (parser == NULL || element_capacity == 0U ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        first_slot > SIZE_MAX - element_capacity) {
        return false;
    }
    probe = *parser;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(
                &probe, probe.current.span, probe.current.kind, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length || !minic_parser_advance(&probe)) {
            return false;
        }
        total_length += decoded_length;
    }
    if (total_length > element_capacity) {
        minic_parser_error(parser, "string initializer is too long for character array");
        return false;
    }
    slot_index = first_slot;
    slot_limit = first_slot + element_capacity;
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        MinicSourceSpan literal_span;

        literal_span = parser->current.span;
        if (!replace_zero_string_payload(parser,
                                         literal_span,
                                         MINIC_TOKEN_STRING_LITERAL,
                                         object_id,
                                         &slot_index,
                                         slot_limit) ||
            !minic_parser_advance(parser)) {
            return false;
        }
    }
    while (slot_index < slot_limit) {
        if (!minic_c0_global_object_replace_zero_initializer_bits(
                parser->program, object_id, slot_index, 0U)) {
            minic_parser_error(parser,
                               "backward string initializer can only replace implicit zero slots");
            return false;
        }
        slot_index += 1U;
    }
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit('parser_string insertion anchor changed')
string_path.write_text(text.replace(anchor, helper + anchor, 1))

# Static aggregate owner: a backward aggregate designator may overwrite only the
# zero subobject that was materialized implicitly when a later field appeared first.
global_path = Path('src/frontend/parser_global.c')
text = global_path.read_text()
anchor = '''static bool parse_static_record_constant(MinicParser *parser,\n'''
helper = r'''static bool overwrite_static_zero_record_constant(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  size_t record_base_slot);

static bool overwrite_static_zero_field_value(MinicParser *parser,
                                              MinicGlobalObjectId object_id,
                                              const MinicRecordField *field,
                                              size_t field_base_slot) {
    if (parser == NULL || field == NULL || field->element_count == 0U ||
        field->is_flexible_array) {
        return false;
    }
    if (field->element_count == 1U && !field->is_array) {
        if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
            return parse_static_scalar_constant_at(
                parser, object_id, field->type, true, field_base_slot);
        }
        if (minic_type_is_record(field->type)) {
            const MinicRecord *nested_record;

            nested_record = minic_c0_program_record(parser->program, field->type.record_id);
            return nested_record != NULL &&
                   overwrite_static_zero_record_constant(
                       parser, object_id, nested_record, field_base_slot);
        }
    }
    if (field->is_array && minic_type_is_char_integer(field->type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return minic_parser_replace_zero_bounded_string_literal_initializer(
            parser, object_id, field_base_slot, field->element_count);
    }
    minic_parser_error(parser,
                       "backward aggregate designator currently requires scalar, record, or character-array zero subobjects");
    return false;
}

static bool overwrite_static_zero_record_constant(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  size_t record_base_slot) {
    size_t field_index;
    size_t field_limit;

    if (parser == NULL || record == NULL || !record->is_complete ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        size_t relative_slot;
        size_t field_base_slot;

        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicRecordFieldPath field_path;
            MinicSourceSpan designator_span;

            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected member name after '.' in initializer");
                return false;
            }
            designator_span = parser->current.span;
            if (!minic_parser_find_record_field_path(parser, record, designator_span, &field_path) ||
                !field_path.found || field_path.ambiguous || field_path.depth != 1U) {
                minic_parser_error(parser,
                                   "backward aggregate initializer requires a direct unambiguous member");
                return false;
            }
            field_index = field_path.field_indices[0];
            if (!minic_parser_advance(parser) ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_EQUAL, "expected '=' after static record designator")) {
                return false;
            }
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many backward aggregate initializer fields");
            return false;
        }
        field = &record->fields[field_index];
        if (!minic_c0_global_record_field_initializer_slot(
                parser->program, record, field_index, &relative_slot) ||
            record_base_slot > SIZE_MAX - relative_slot) {
            minic_parser_error(parser, "cannot locate backward aggregate initializer field slot");
            return false;
        }
        field_base_slot = record_base_slot + relative_slot;
        if (!overwrite_static_zero_field_value(parser, object_id, field, field_base_slot)) {
            return false;
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
            minic_parser_error(parser, "expected ',' or '}' in backward aggregate initializer");
            return false;
        }
    }
    return minic_parser_expect(parser,
                               MINIC_TOKEN_RBRACE,
                               "expected '}' after backward aggregate initializer");
}

'''
if text.count(anchor) != 1:
    raise SystemExit('parser_global insertion anchor changed')
text = text.replace(anchor, helper + anchor, 1)

old = r'''        } else if (overwrite_materialized_field) {
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
'''
new = r'''        } else if (overwrite_materialized_field) {
            size_t relative_slot;
            size_t slot_index;

            if (!minic_c0_global_record_field_initializer_slot(
                    parser->program, record, field_index, &relative_slot) ||
                record_base_slot > SIZE_MAX - relative_slot) {
                minic_parser_error(parser, "cannot locate backward static record designator slot");
                return false;
            }
            slot_index = record_base_slot + relative_slot;
            if (!overwrite_static_zero_field_value(parser, object_id, field, slot_index)) {
                return false;
            }
'''
if text.count(old) != 1:
    raise SystemExit(f'backward direct-field block changed: {text.count(old)}')
text = text.replace(old, new, 1)
global_path.write_text(text)
