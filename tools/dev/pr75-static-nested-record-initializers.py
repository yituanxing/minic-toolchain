#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# Frontend: recursively flatten integer/null-pointer aggregate leaves for static
# record objects. Direct function-pointer records retain the existing relocation
# path, so this extends data aggregates without disturbing that contract.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
text = replace_once(
    text,
    "#include <stdint.h>\n#include <stdlib.h>\n",
    "#include <limits.h>\n#include <stdint.h>\n#include <stdlib.h>\n",
    "parser_global limits include",
)
marker = "static bool parse_static_record_field_initializer(MinicParser *parser,\n"
helper = r'''static bool static_record_has_direct_function_pointer(const MinicRecord *record) {
    size_t field_index;

    if (record == NULL) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        MinicType pointee;

        field = &record->fields[field_index];
        if (field->element_count == 1U && minic_type_is_pointer(field->type) &&
            minic_type_pointee(field->type, &pointee) && minic_type_is_function(pointee)) {
            return true;
        }
    }
    return false;
}

static bool append_static_constant_zero(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        MinicType type);

static bool append_static_field_zeros(MinicParser *parser,
                                      MinicGlobalObjectId object_id,
                                      const MinicRecordField *field) {
    size_t element_index;

    if (field == NULL || field->element_count == 0U) {
        return false;
    }
    for (element_index = 0U; element_index < field->element_count; ++element_index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            return false;
        }
    }
    return true;
}

static bool append_static_constant_zero(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return minic_c0_global_object_add_initializer(parser->program, object_id, 0);
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_index;

        array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            if (!append_static_constant_zero(parser, object_id, array_type->element_type)) {
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
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool parse_static_constant_value(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        MinicType type);

static bool parse_static_scalar_constant(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         MinicType type) {
    bool braced;

    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(type)) {
        int64_t parsed;

        if (!minic_parser_parse_integer_constant_expression(parser, &parsed) || parsed < INT_MIN ||
            parsed > INT_MAX ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, (int)parsed)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "static aggregate integer initializer is out of range");
            }
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        if (!parse_zero_pointer_constant(parser) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static null-pointer initializer");
            }
            return false;
        }
    } else {
        return false;
    }
    if (!braced) {
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after scalar initializer");
}

static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    size_t element_index;

    if (array_type == NULL || array_type->element_count == 0U ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in array initializer")) {
        return false;
    }
    element_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (element_index >= array_type->element_count) {
            minic_parser_error(parser, "too many nested static array initializers");
            return false;
        }
        if (!parse_static_constant_value(parser, object_id, array_type->element_type)) {
            return false;
        }
        element_index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in nested static array initializer");
            return false;
        }
    }
    while (element_index < array_type->element_count) {
        if (!append_static_constant_zero(parser, object_id, array_type->element_type)) {
            minic_parser_error(parser, "cannot zero-fill nested static array initializer");
            return false;
        }
        element_index += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after array initializer");
}

static bool parse_static_record_constant(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const MinicRecord *record) {
    size_t field_index;
    size_t field_limit;

    if (record == NULL || !record->is_complete || record->field_count == 0U ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        size_t element_index;

        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many nested static record initializers");
            return false;
        }
        field = &record->fields[field_index];
        if (field->element_count == 0U || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported nested static record field");
            return false;
        }
        if (field->element_count == 1U) {
            if (!parse_static_constant_value(parser, object_id, field->type)) {
                return false;
            }
        } else {
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_LBRACE, "expected '{' in record field array initializer")) {
                return false;
            }
            element_index = 0U;
            while (parser->current.kind != MINIC_TOKEN_RBRACE) {
                if (element_index >= field->element_count ||
                    !parse_static_constant_value(parser, object_id, field->type)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser, "too many record field array initializers");
                    }
                    return false;
                }
                element_index += 1U;
                if (parser->current.kind == MINIC_TOKEN_COMMA) {
                    if (!minic_parser_advance(parser)) {
                        return false;
                    }
                    if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                        break;
                    }
                } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                    minic_parser_error(parser,
                                       "expected ',' or '}' in record field array initializer");
                    return false;
                }
            }
            while (element_index < field->element_count) {
                if (!append_static_constant_zero(parser, object_id, field->type)) {
                    return false;
                }
                element_index += 1U;
            }
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_RBRACE, "expected '}' after record field array initializer")) {
                return false;
            }
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
    while (field_index < field_limit) {
        if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
            minic_parser_error(parser, "cannot zero-fill nested static record initializer");
            return false;
        }
        field_index += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer");
}

static bool parse_static_constant_value(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return parse_static_scalar_constant(parser, object_id, type);
    }
    if (minic_type_is_array(type)) {
        return parse_static_array_constant(
            parser, object_id, minic_c0_program_array_type(parser->program, type.array_type_id));
    }
    if (minic_type_is_record(type)) {
        return parse_static_record_constant(
            parser, object_id, minic_c0_program_record(parser->program, type.record_id));
    }
    minic_parser_error(parser, "unsupported nested static aggregate initializer type");
    return false;
}

static bool parse_static_nested_record_object(MinicParser *parser,
                                              MinicType type,
                                              MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            type,
                                            true,
                                            minic_type_is_const(type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !parse_static_constant_value(parser, object_id, type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse nested static record initializer");
        }
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"static record field initializer marker count={text.count(marker)}")
text = text.replace(marker, helper + marker, 1)
old = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser, type, name_span);
    }
    if (!minic_c0_program_add_global_object(parser->program,
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser, type, name_span);
    }
    if (!static_record_has_direct_function_pointer(record)) {
        return parse_static_nested_record_object(parser, type, name_span);
    }
    if (!minic_c0_program_add_global_object(parser->program,
'''
if text.count(old) != 1:
    raise SystemExit(f"nested static record dispatch count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Backend: consume the flattened scalar leaves recursively while using the
# completed RV64 record layout for all field offsets and padding.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
start_marker = "static bool minic_riscv64_emit_record_values(FILE *file,\n"
end_marker = "static bool minic_riscv64_record_array_info("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate staged record value emitter")
replacement = r'''static bool minic_riscv64_emit_constant_value(FILE *file,
                                                const MinicC0Program *program,
                                                const MinicGlobalObject *object,
                                                MinicType type,
                                                size_t *initializer_index,
                                                size_t *emitted_size) {
    size_t type_size;
    size_t type_alignment;

    if (file == NULL || program == NULL || object == NULL || initializer_index == NULL ||
        emitted_size == NULL ||
        !minic_riscv64_type_layout(program, type, &type_size, &type_alignment)) {
        return false;
    }
    (void)type_alignment;
    if (minic_type_is_integer(type)) {
        const char *directive;
        int value;

        if (*initializer_index >= object->initializer_count) {
            return false;
        }
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
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_pointer(type)) {
        int value;

        if (*initializer_index >= object->initializer_count) {
            return false;
        }
        value = object->initializer_values[*initializer_index];
        *initializer_index += 1U;
        if (value != 0 || !minic_riscv64_emit_zero_bytes(file, type_size)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t cursor;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        cursor = 0U;
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t element_emitted;

            if (!minic_riscv64_emit_constant_value(file,
                                                   program,
                                                   object,
                                                   array_type->element_type,
                                                   initializer_index,
                                                   &element_emitted) ||
                cursor > type_size - element_emitted) {
                return false;
            }
            cursor += element_emitted;
        }
        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t cursor;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        cursor = 0U;
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t element_index;
            size_t field_offset;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            field_offset = record->is_union ? 0U : field->storage_offset;
            if (field_offset < cursor || field_offset > type_size ||
                !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
                return false;
            }
            cursor = field_offset;
            for (element_index = 0U; element_index < field->element_count; ++element_index) {
                size_t element_emitted;

                if (!minic_riscv64_emit_constant_value(file,
                                                       program,
                                                       object,
                                                       field->type,
                                                       initializer_index,
                                                       &element_emitted) ||
                    cursor > type_size - element_emitted) {
                    return false;
                }
                cursor += element_emitted;
            }
            if (record->is_union) {
                break;
            }
        }
        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    return false;
}

static bool minic_riscv64_emit_record_values(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalObject *object) {
    size_t emitted_size;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized || object->function_relocation_count != 0U ||
        object->object_relocation_count != 0U) {
        return false;
    }
    initializer_index = 0U;
    emitted_size = 0U;
    return minic_riscv64_emit_constant_value(file,
                                             program,
                                             object,
                                             object->type,
                                             &initializer_index,
                                             &emitted_size) &&
           initializer_index == object->initializer_count && emitted_size == object->storage_size;
}

'''
text = text[:start] + replacement + text[end:]
old = '''    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || record->is_union ||
            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
            object->initializer_count != record->field_count) {
            return false;
        }
    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {
'''
new = '''    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || object->function_relocation_count != 0U ||
            object->object_relocation_count != 0U || object->initializer_count == 0U) {
            return false;
        }
    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {
'''
if text.count(old) != 1:
    raise SystemExit(f"nested record validation count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged recursive static record aggregate constant initializers")
