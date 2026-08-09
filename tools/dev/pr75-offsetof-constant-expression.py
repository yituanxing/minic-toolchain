#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()
marker = "static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {\n"
start = text.find(marker)
if start < 0 or text.find(marker, start + 1) >= 0:
    raise SystemExit("cannot uniquely locate integer constant-expression primary parser")

helper = r'''static bool constant_align_up(uint64_t value, uint64_t alignment, uint64_t *result) {
    uint64_t remainder;
    uint64_t padding;

    if (alignment == 0U || result == NULL) {
        return false;
    }
    remainder = value % alignment;
    padding = remainder == 0U ? 0U : alignment - remainder;
    if (value > UINT64_MAX - padding) {
        return false;
    }
    *result = value + padding;
    return true;
}

static bool constant_type_layout(const MinicC0Program *program,
                                 MinicType type,
                                 unsigned int depth,
                                 uint64_t *size,
                                 uint64_t *alignment) {
    if (program == NULL || size == NULL || alignment == NULL || depth > 64U) {
        return false;
    }
    if (minic_type_is_pointer(type)) {
        *size = 8U;
        *alignment = 8U;
        return true;
    }
    if (minic_type_is_integer(type)) {
        switch (type.integer_rank) {
        case MINIC_INTEGER_RANK_CHAR:
            *size = 1U;
            *alignment = 1U;
            return true;
        case MINIC_INTEGER_RANK_SHORT:
            *size = 2U;
            *alignment = 2U;
            return true;
        case MINIC_INTEGER_RANK_INT:
            *size = 4U;
            *alignment = 4U;
            return true;
        case MINIC_INTEGER_RANK_LONG:
        case MINIC_INTEGER_RANK_LONG_LONG:
            *size = 8U;
            *alignment = 8U;
            return true;
        case MINIC_INTEGER_RANK_NONE:
            return false;
        }
    }
    if (minic_type_is_float(type)) {
        *size = 4U;
        *alignment = 4U;
        return true;
    }
    if (minic_type_is_double(type)) {
        *size = 8U;
        *alignment = 8U;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        uint64_t element_size;
        uint64_t element_alignment;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !constant_type_layout(program,
                                  array_type->element_type,
                                  depth + 1U,
                                  &element_size,
                                  &element_alignment) ||
            element_size > UINT64_MAX / array_type->element_count) {
            return false;
        }
        *size = element_size * array_type->element_count;
        *alignment = element_alignment;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        uint64_t storage_size;
        uint64_t record_alignment;
        size_t field_index;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        storage_size = 0U;
        record_alignment = 1U;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;
            uint64_t element_size;
            uint64_t field_size;
            uint64_t field_alignment;
            uint64_t field_offset;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U ||
                !constant_type_layout(program,
                                      field->type,
                                      depth + 1U,
                                      &element_size,
                                      &field_alignment) ||
                element_size > UINT64_MAX / field->element_count) {
                return false;
            }
            field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
            if (record->is_union) {
                field_offset = 0U;
                if (field_size > storage_size) {
                    storage_size = field_size;
                }
            } else {
                if (record->is_packed) {
                    field_offset = storage_size;
                } else if (!constant_align_up(storage_size, field_alignment, &field_offset)) {
                    return false;
                }
                if (field_offset > UINT64_MAX - field_size) {
                    return false;
                }
                storage_size = field_offset + field_size;
            }
            if (!record->is_packed && field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
        }
        if (!constant_align_up(storage_size, record_alignment, size)) {
            return false;
        }
        *alignment = record_alignment;
        return true;
    }
    return false;
}

static bool constant_record_member_offset(const MinicC0Program *program,
                                          const MinicRecord *record,
                                          const char *name,
                                          size_t name_length,
                                          uint64_t *offset) {
    uint64_t storage_size;
    size_t field_index;

    if (program == NULL || record == NULL || name == NULL || offset == NULL ||
        !record->is_complete) {
        return false;
    }
    storage_size = 0U;
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        uint64_t element_size;
        uint64_t field_size;
        uint64_t field_alignment;
        uint64_t field_offset;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count == 0U ||
            !constant_type_layout(
                program, field->type, 0U, &element_size, &field_alignment) ||
            element_size > UINT64_MAX / field->element_count) {
            return false;
        }
        field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
        if (record->is_union) {
            field_offset = 0U;
        } else if (record->is_packed) {
            field_offset = storage_size;
        } else if (!constant_align_up(storage_size, field_alignment, &field_offset)) {
            return false;
        }
        if (field->name_length == name_length && memcmp(field->name, name, name_length) == 0) {
            *offset = field_offset;
            return true;
        }
        if (!record->is_union) {
            if (field_offset > UINT64_MAX - field_size) {
                return false;
            }
            storage_size = field_offset + field_size;
        }
    }
    return false;
}

static bool current_is_builtin_offsetof_constant(const MinicParser *parser) {
    static const char name[] = "__builtin_offsetof";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(name) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool parse_offsetof_integer_constant(MinicParser *parser, int64_t *value) {
    MinicSourceSpan field_span;
    MinicType record_type;
    const MinicRecord *record;
    uint64_t offset;
    size_t field_name_length;

    if (parser == NULL || value == NULL || !current_is_builtin_offsetof_constant(parser) ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_offsetof") ||
        !minic_parser_parse_type_name(parser, &record_type)) {
        return false;
    }
    if (!minic_type_is_record(record_type)) {
        minic_parser_error(parser, "__builtin_offsetof requires a record type");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected direct record field in __builtin_offsetof");
        }
        return false;
    }
    field_span = parser->current.span;
    field_name_length = minic_parser_span_length(field_span);
    if (!constant_record_member_offset(parser->program,
                                       record,
                                       parser->source + field_span.begin.offset,
                                       field_name_length,
                                       &offset) ||
        offset > (uint64_t)INT64_MAX || !minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_RPAREN,
                             "expected ')' after __builtin_offsetof")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot fold __builtin_offsetof in integer constant expression");
        }
        return false;
    }
    *value = (int64_t)offset;
    return true;
}

'''
text = text[:start] + helper + text[start:]
primary_start = text.index(marker, start + len(helper))
needle = '''    if (parser == NULL || value == NULL) {
        return false;
    }
'''
position = text.find(needle, primary_start)
if position < 0:
    raise SystemExit("cannot locate constant-expression primary prologue")
insert_at = position + len(needle)
text = (
    text[:insert_at]
    + '''    if (current_is_builtin_offsetof_constant(parser)) {
        return parse_offsetof_integer_constant(parser, value);
    }
'''
    + text[insert_at:]
)
path.write_text(text)
print("staged target-layout __builtin_offsetof in shared integer constant expressions")
