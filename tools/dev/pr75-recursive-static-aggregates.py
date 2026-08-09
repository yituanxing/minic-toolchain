#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# Frontend: keep the existing static-record parser as the relocation-capable
# legacy path, and route relocation-free scalar aggregates through a recursive
# positional initializer. The recursive representation is a flat sequence of
# scalar leaves: integer constants are stored as int payloads, null pointers as
# zero. Structs consume all fields, while unions initialize their first member.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
old_name = "static bool parse_static_record(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {"
if text.count(old_name) != 1:
    raise SystemExit(f"static record parser: expected 1 match, found {text.count(old_name)}")
text = text.replace(old_name,
                    "static bool parse_static_record_legacy(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {",
                    1)

marker = "bool minic_parser_parse_typedef(MinicParser *parser) {\n"
helper = r'''static bool static_aggregate_append_zero(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         MinicType type);

static bool static_aggregate_append_field_zero(MinicParser *parser,
                                               MinicGlobalObjectId object_id,
                                               const MinicRecordField *field) {
    size_t index;

    if (field == NULL || field->is_flexible_array || field->element_count == 0U) {
        return false;
    }
    for (index = 0U; index < field->element_count; ++index) {
        if (!static_aggregate_append_zero(parser, object_id, field->type)) {
            return false;
        }
    }
    return true;
}

static bool static_aggregate_append_zero(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return minic_c0_global_object_add_initializer(parser->program, object_id, 0);
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t index;

        array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        for (index = 0U; index < array_type->element_count; ++index) {
            if (!static_aggregate_append_zero(parser, object_id, array_type->element_type)) {
                return false;
            }
        }
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            if (!static_aggregate_append_field_zero(
                    parser, object_id, minic_c0_record_field(record, field_index))) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool parse_static_aggregate_initializer(MinicParser *parser,
                                               MinicGlobalObjectId object_id,
                                               MinicType type);

static bool parse_static_aggregate_field(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const MinicRecordField *field) {
    size_t index;

    if (field == NULL || field->is_flexible_array || field->element_count == 0U) {
        minic_parser_error(parser, "unsupported field in recursive static aggregate initializer");
        return false;
    }
    if (field->element_count == 1U) {
        return parse_static_aggregate_initializer(parser, object_id, field->type);
    }
    if (!minic_parser_expect(parser,
                             MINIC_TOKEN_LBRACE,
                             "expected '{' before static aggregate field array")) {
        return false;
    }
    index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (index >= field->element_count) {
            minic_parser_error(parser, "too many static aggregate field-array initializers");
            return false;
        }
        if (!parse_static_aggregate_initializer(parser, object_id, field->type)) {
            return false;
        }
        index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static aggregate field array");
            return false;
        }
    }
    while (index < field->element_count) {
        if (!static_aggregate_append_zero(parser, object_id, field->type)) {
            minic_parser_error(parser, "cannot zero-fill static aggregate field array");
            return false;
        }
        index += 1U;
    }
    return minic_parser_expect(parser,
                               MINIC_TOKEN_RBRACE,
                               "expected '}' after static aggregate field array");
}

static bool parse_static_aggregate_initializer(MinicParser *parser,
                                               MinicGlobalObjectId object_id,
                                               MinicType type) {
    if (minic_type_is_integer(type)) {
        int64_t value;

        if (!minic_parser_parse_integer_constant_expression(parser, &value)) {
            return false;
        }
        if (value < INT_MIN || value > INT_MAX ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, (int)value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "integer static aggregate initializer is out of supported range");
            }
            return false;
        }
        return true;
    }
    if (minic_type_is_pointer(type)) {
        if (!minic_parser_parse_zero_pointer_constant(parser) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "static aggregate pointer initializer must be a null pointer constant");
            }
            return false;
        }
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t index;

        array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !minic_parser_expect(parser,
                                 MINIC_TOKEN_LBRACE,
                                 "expected '{' before static aggregate array")) {
            return false;
        }
        index = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            if (index >= array_type->element_count) {
                minic_parser_error(parser, "too many static aggregate array initializers");
                return false;
            }
            if (!parse_static_aggregate_initializer(
                    parser, object_id, array_type->element_type)) {
                return false;
            }
            index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in static aggregate array");
                return false;
            }
        }
        while (index < array_type->element_count) {
            if (!static_aggregate_append_zero(parser, object_id, array_type->element_type)) {
                minic_parser_error(parser, "cannot zero-fill static aggregate array");
                return false;
            }
            index += 1U;
        }
        return minic_parser_expect(parser,
                                   MINIC_TOKEN_RBRACE,
                                   "expected '}' after static aggregate array");
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U ||
            !minic_parser_expect(parser,
                                 MINIC_TOKEN_LBRACE,
                                 "expected '{' before static aggregate record")) {
            return false;
        }
        field_index = 0U;
        field_limit = record->is_union ? 1U : record->field_count;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            if (field_index >= field_limit) {
                minic_parser_error(parser, "too many static aggregate record initializers");
                return false;
            }
            if (!parse_static_aggregate_field(
                    parser, object_id, minic_c0_record_field(record, field_index))) {
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
                minic_parser_error(parser, "expected ',' or '}' in static aggregate record");
                return false;
            }
        }
        while (field_index < field_limit) {
            if (!static_aggregate_append_field_zero(
                    parser, object_id, minic_c0_record_field(record, field_index))) {
                minic_parser_error(parser, "cannot zero-fill static aggregate record");
                return false;
            }
            field_index += 1U;
        }
        return minic_parser_expect(parser,
                                   MINIC_TOKEN_RBRACE,
                                   "expected '}' after static aggregate record");
    }

    minic_parser_error(parser, "unsupported scalar leaf in recursive static aggregate initializer");
    return false;
}

static bool record_has_direct_function_pointer(const MinicC0Program *program, MinicType type) {
    const MinicRecord *record;
    size_t field_index;

    if (program == NULL || !minic_type_is_record(type)) {
        return false;
    }
    record = minic_c0_program_record(program, type.record_id);
    if (record == NULL || !record->is_complete) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        MinicType pointee;

        field = minic_c0_record_field(record, field_index);
        if (field != NULL && field->element_count == 1U &&
            minic_type_pointee(field->type, &pointee) && minic_type_is_function(pointee)) {
            return true;
        }
    }
    return false;
}

static bool parse_static_record(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (parser == NULL || !minic_type_is_record(type)) {
        return false;
    }
    /* Preserve the existing function-relocation path and record-array path. */
    if (parser->current.kind == MINIC_TOKEN_LBRACKET ||
        parser->current.kind == MINIC_TOKEN_SEMICOLON ||
        record_has_direct_function_pointer(parser->program, type)) {
        return parse_static_record_legacy(parser, type, name_span);
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            type,
                                            true,
                                            minic_type_is_const(type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record") ||
        !parse_static_aggregate_initializer(parser, object_id, type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static aggregate")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse recursive static aggregate initializer");
        }
        return false;
    }
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit("cannot locate typedef parser anchor for recursive static aggregate")
text = text.replace(marker, helper + marker, 1)
path.write_text(text)


# Backend: recursively consume the same scalar-leaf sequence according to target
# layout. This keeps parser semantics target-independent while reusing the RV64
# record/array layout code as the single source of truth for padding and stride.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
start_marker = "static bool minic_riscv64_emit_record_values(FILE *file,\n"
end_marker = "static bool minic_riscv64_record_array_info(const MinicC0Program *program,\n"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate recursive record backend replacement range")
backend = r'''static bool minic_riscv64_aggregate_leaf_count(const MinicC0Program *program,
                                               MinicType type,
                                               size_t *count) {
    size_t result;

    if (program == NULL || count == NULL) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        *count = 1U;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_count;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !minic_riscv64_aggregate_leaf_count(program, array_type->element_type, &element_count) ||
            element_count > SIZE_MAX / array_type->element_count) {
            return false;
        }
        *count = element_count * array_type->element_count;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        result = 0U;
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t leaf_count;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->is_flexible_array || field->element_count == 0U ||
                !minic_riscv64_aggregate_leaf_count(program, field->type, &leaf_count) ||
                leaf_count > SIZE_MAX / field->element_count ||
                result > SIZE_MAX - leaf_count * field->element_count) {
                return false;
            }
            result += leaf_count * field->element_count;
        }
        *count = result;
        return true;
    }
    return false;
}

static bool minic_riscv64_emit_aggregate_leaf(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalObject *object,
                                              MinicType type,
                                              size_t base_offset,
                                              size_t *cursor,
                                              size_t *initializer_index);

static bool minic_riscv64_emit_aggregate_gap(FILE *file,
                                             size_t target_offset,
                                             size_t *cursor) {
    if (file == NULL || cursor == NULL || target_offset < *cursor ||
        !minic_riscv64_emit_zero_bytes(file, target_offset - *cursor)) {
        return false;
    }
    *cursor = target_offset;
    return true;
}

static bool minic_riscv64_emit_integer_leaf(FILE *file,
                                            const MinicC0Program *program,
                                            const MinicGlobalObject *object,
                                            MinicType type,
                                            size_t base_offset,
                                            size_t *cursor,
                                            size_t *initializer_index) {
    const char *directive;
    size_t size;
    size_t alignment;
    int value;

    if (!minic_riscv64_type_layout(program, type, &size, &alignment) || size == 0U ||
        *initializer_index >= object->initializer_count ||
        !minic_riscv64_emit_aggregate_gap(file, base_offset, cursor)) {
        return false;
    }
    (void)alignment;
    value = object->initializer_values[*initializer_index];
    *initializer_index += 1U;
    directive = minic_type_is_char_integer(type)    ? ".byte"
                : minic_type_is_short_integer(type) ? ".half"
                : minic_type_is_long_integer(type)  ? ".dword"
                                                     : ".word";
    if (minic_type_is_char_integer(type)) {
        unsigned int byte_value;

        byte_value = (unsigned int)value & 0xffU;
        if (fprintf(file, "  %s %u\n", directive, byte_value) < 0) {
            return false;
        }
    } else if (fprintf(file, "  %s %d\n", directive, value) < 0) {
        return false;
    }
    *cursor = base_offset + size;
    return true;
}

static bool minic_riscv64_emit_aggregate_leaf(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalObject *object,
                                              MinicType type,
                                              size_t base_offset,
                                              size_t *cursor,
                                              size_t *initializer_index) {
    size_t aggregate_size;
    size_t aggregate_alignment;

    if (file == NULL || program == NULL || object == NULL || cursor == NULL ||
        initializer_index == NULL ||
        !minic_riscv64_type_layout(program, type, &aggregate_size, &aggregate_alignment)) {
        return false;
    }
    (void)aggregate_alignment;

    if (minic_type_is_integer(type)) {
        return minic_riscv64_emit_integer_leaf(
            file, program, object, type, base_offset, cursor, initializer_index);
    }
    if (minic_type_is_pointer(type)) {
        if (*initializer_index >= object->initializer_count ||
            object->initializer_values[*initializer_index] != 0 ||
            !minic_riscv64_emit_aggregate_gap(file, base_offset, cursor) ||
            !minic_riscv64_emit_zero_bytes(file, aggregate_size)) {
            return false;
        }
        *initializer_index += 1U;
        *cursor = base_offset + aggregate_size;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_size;
        size_t element_alignment;
        size_t index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !minic_riscv64_type_layout(
                program, array_type->element_type, &element_size, &element_alignment)) {
            return false;
        }
        (void)element_alignment;
        for (index = 0U; index < array_type->element_count; ++index) {
            if (!minic_riscv64_emit_aggregate_leaf(file,
                                                   program,
                                                   object,
                                                   array_type->element_type,
                                                   base_offset + index * element_size,
                                                   cursor,
                                                   initializer_index)) {
                return false;
            }
        }
        return minic_riscv64_emit_aggregate_gap(file, base_offset + aggregate_size, cursor);
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t element_size;
            size_t element_alignment;
            size_t element_index;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->is_flexible_array || field->element_count == 0U ||
                !minic_riscv64_type_layout(
                    program, field->type, &element_size, &element_alignment)) {
                return false;
            }
            (void)element_alignment;
            for (element_index = 0U; element_index < field->element_count; ++element_index) {
                if (!minic_riscv64_emit_aggregate_leaf(file,
                                                       program,
                                                       object,
                                                       field->type,
                                                       base_offset + field->storage_offset +
                                                           element_index * element_size,
                                                       cursor,
                                                       initializer_index)) {
                    return false;
                }
            }
        }
        return minic_riscv64_emit_aggregate_gap(file, base_offset + aggregate_size, cursor);
    }
    return false;
}

static bool minic_riscv64_emit_record_values(FILE *file,
                                               const MinicC0Program *program,
                                               const MinicGlobalObject *object) {
    size_t cursor;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized || object->function_relocation_count != 0U ||
        object->object_relocation_count != 0U) {
        return false;
    }
    cursor = 0U;
    initializer_index = 0U;
    if (!minic_riscv64_emit_aggregate_leaf(
            file, program, object, object->type, 0U, &cursor, &initializer_index)) {
        return false;
    }
    return initializer_index == object->initializer_count && cursor == object->storage_size;
}

'''
text = text[:start] + backend + text[end:]

old_validation = '''    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || record->is_union ||
            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
            object->initializer_count != record->field_count) {
            return false;
        }
'''
new_validation = '''    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;
        size_t leaf_count;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete ||
            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
            !minic_riscv64_aggregate_leaf_count(program, object->type, &leaf_count) ||
            object->initializer_count != leaf_count) {
            return false;
        }
'''
if text.count(old_validation) != 1:
    raise SystemExit(f"record validation: expected 1 match, found {text.count(old_validation)}")
text = text.replace(old_validation, new_validation, 1)
path.write_text(text)

print("staged recursive static struct/union aggregates with integer/null-pointer leaves")
