#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# Frontend: parse one-dimensional arrays of complete structs with direct integer
# fields. Initializer values are flattened element-major; missing fields/elements
# are recorded as zero so the backend can reproduce the target record layout.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
text = replace_once(
    text,
    '#include <stdint.h>\n#include <stdlib.h>\n',
    '#include <limits.h>\n#include <stdint.h>\n#include <stdlib.h>\n',
    "parser_global limits include",
)
marker = '''static bool parse_static_record(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {\n'''
helper = r'''static bool static_record_array_append_value(int **values,
                                             size_t *value_count,
                                             size_t *value_capacity,
                                             int value) {
    int *resized;
    size_t new_capacity;

    if (values == NULL || value_count == NULL || value_capacity == NULL) {
        return false;
    }
    if (*value_count == *value_capacity) {
        new_capacity = *value_capacity == 0U ? 16U : *value_capacity * 2U;
        if (new_capacity < *value_capacity || new_capacity > SIZE_MAX / sizeof(**values)) {
            return false;
        }
        resized = (int *)realloc(*values, new_capacity * sizeof(**values));
        if (resized == NULL) {
            return false;
        }
        *values = resized;
        *value_capacity = new_capacity;
    }
    (*values)[*value_count] = value;
    *value_count += 1U;
    return true;
}

static bool parse_static_record_array(MinicParser *parser,
                                      MinicType element_type,
                                      MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    int *values;
    size_t value_count;
    size_t value_capacity;
    size_t element_count;
    size_t declared_count;
    bool inferred_bound;
    bool success;
    size_t field_index;

    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-empty struct type");
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_type_is_integer(field->type)) {
            minic_parser_error(parser,
                               "static record array currently requires direct scalar integer fields");
            return false;
        }
    }

    values = NULL;
    value_count = 0U;
    value_capacity = 0U;
    element_count = 0U;
    declared_count = 0U;
    inferred_bound = false;
    success = false;

    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            goto done;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &declared_count)) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record array initializer")) {
        goto done;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (!inferred_bound && element_count >= declared_count) {
            minic_parser_error(parser, "too many static record array initializers");
            goto done;
        }
        if (!minic_parser_expect(parser,
                                 MINIC_TOKEN_LBRACE,
                                 "expected '{' before record array element")) {
            goto done;
        }

        field_index = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            int64_t parsed_value;

            if (field_index >= record->field_count) {
                minic_parser_error(parser, "too many fields in static record array element");
                goto done;
            }
            if (!minic_parser_parse_integer_constant_expression(parser, &parsed_value)) {
                goto done;
            }
            if (parsed_value < INT_MIN || parsed_value > INT_MAX) {
                minic_parser_error(parser,
                                   "static record array initializer is out of supported range");
                goto done;
            }
            if (!static_record_array_append_value(
                    &values, &value_count, &value_capacity, (int)parsed_value)) {
                minic_parser_error(parser, "out of memory while recording record array initializer");
                goto done;
            }
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    goto done;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in record array element");
                goto done;
            }
        }
        while (field_index < record->field_count) {
            if (!static_record_array_append_value(&values, &value_count, &value_capacity, 0)) {
                minic_parser_error(parser, "out of memory while zero-filling record array element");
                goto done;
            }
            field_index += 1U;
        }
        if (!minic_parser_expect(parser,
                                 MINIC_TOKEN_RBRACE,
                                 "expected '}' after record array element")) {
            goto done;
        }
        element_count += 1U;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' after record array element");
            goto done;
        }
    }
    if (!minic_parser_expect(parser,
                             MINIC_TOKEN_RBRACE,
                             "expected '}' after static record array initializer")) {
        goto done;
    }
    if (element_count == 0U) {
        minic_parser_error(parser, "static record array requires at least one initializer");
        goto done;
    }
    if (inferred_bound) {
        declared_count = element_count;
    } else {
        while (element_count < declared_count) {
            for (field_index = 0U; field_index < record->field_count; ++field_index) {
                if (!static_record_array_append_value(&values, &value_count, &value_capacity, 0)) {
                    minic_parser_error(parser, "out of memory while zero-filling record array");
                    goto done;
                }
            }
            element_count += 1U;
        }
    }
    if (record->field_count > SIZE_MAX / declared_count ||
        value_count != record->field_count * declared_count) {
        minic_parser_error(parser, "invalid static record array initializer shape");
        goto done;
    }

    if (!minic_c0_program_add_array_type(
            parser->program, element_type, declared_count, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot create static record array object");
        goto done;
    }
    for (field_index = 0U; field_index < value_count; ++field_index) {
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, values[field_index])) {
            minic_parser_error(parser, "cannot record static record array initializer value");
            goto done;
        }
    }
    success =
        minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");

done:
    free(values);
    return success;
}

'''
text = replace_once(text, marker, helper + marker, "static record array parser anchor")
text = replace_once(
    text,
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        minic_parser_error(parser, "static record array globals are not supported");\n        return false;\n    }\n''',
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        return parse_static_record_array(parser, type, name_span);\n    }\n''',
    "static record array dispatch",
)
path.write_text(text)

# Backend: recognize arrays whose direct element type is a complete struct and
# emit flattened field values using each field's target offset and the record stride.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
marker = "static bool minic_riscv64_emit_global_object(FILE *file,\n"
helper = r'''static bool minic_riscv64_record_array_info(const MinicC0Program *program,
                                             MinicType type,
                                             const MinicArrayType **array_type_out,
                                             const MinicRecord **record_out) {
    const MinicArrayType *array_type;
    const MinicRecord *record;

    if (program == NULL || !minic_type_is_array(type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    if (array_type == NULL || !minic_type_is_record(array_type->element_type)) {
        return false;
    }
    record = minic_c0_program_record(program, array_type->element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union) {
        return false;
    }
    if (array_type_out != NULL) {
        *array_type_out = array_type;
    }
    if (record_out != NULL) {
        *record_out = record;
    }
    return true;
}

static bool minic_riscv64_emit_record_array_values(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicGlobalObject *object) {
    const MinicArrayType *array_type;
    const MinicRecord *record;
    size_t element_size;
    size_t element_alignment;
    size_t cursor;
    size_t element_index;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||
        object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
        !minic_riscv64_record_array_info(program, object->type, &array_type, &record) ||
        record->field_count == 0U ||
        array_type->element_count > SIZE_MAX / record->field_count ||
        object->initializer_count != array_type->element_count * record->field_count ||
        !minic_riscv64_type_layout(
            program, array_type->element_type, &element_size, &element_alignment) ||
        element_size == 0U || array_type->element_count > SIZE_MAX / element_size ||
        object->storage_size != array_type->element_count * element_size) {
        return false;
    }
    (void)element_alignment;

    cursor = 0U;
    initializer_index = 0U;
    for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
        size_t field_index;
        size_t element_base;

        element_base = element_index * element_size;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;
            const char *directive;
            size_t field_size;
            size_t field_alignment;
            size_t field_offset;
            int value;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
                !minic_type_is_integer(field->type) ||
                !minic_riscv64_type_layout(
                    program, field->type, &field_size, &field_alignment)) {
                return false;
            }
            (void)field_alignment;
            if (field->storage_offset > element_size ||
                field_size > element_size - field->storage_offset) {
                return false;
            }
            field_offset = element_base + field->storage_offset;
            if (field_offset < cursor || field_offset > object->storage_size ||
                field_size > object->storage_size - field_offset ||
                !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
                return false;
            }

            value = object->initializer_values[initializer_index++];
            directive = minic_type_is_char_integer(field->type)    ? ".byte"
                        : minic_type_is_short_integer(field->type) ? ".half"
                        : minic_type_is_long_integer(field->type)  ? ".dword"
                                                                   : ".word";
            if (minic_type_is_char_integer(field->type)) {
                unsigned int byte_value;

                byte_value = (unsigned int)value & 0xffU;
                if (fprintf(file, "  %s %u\n", directive, byte_value) < 0) {
                    return false;
                }
            } else if (fprintf(file, "  %s %d\n", directive, value) < 0) {
                return false;
            }
            cursor = field_offset + field_size;
        }
        if (cursor > element_base + element_size ||
            !minic_riscv64_emit_zero_bytes(file, element_base + element_size - cursor)) {
            return false;
        }
        cursor = element_base + element_size;
    }
    return initializer_index == object->initializer_count && cursor == object->storage_size;
}

'''
text = replace_once(text, marker, helper + marker, "record array backend anchor")

old = '''    } else if (minic_type_is_record(object->type)) {\n        const MinicRecord *record;\n\n        record = minic_c0_program_record(program, object->type.record_id);\n        if (record == NULL || !record->is_complete || record->is_union ||\n            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||\n            object->initializer_count != record->field_count) {\n            return false;\n        }\n    } else {\n'''
new = '''    } else if (minic_type_is_record(object->type)) {\n        const MinicRecord *record;\n\n        record = minic_c0_program_record(program, object->type.record_id);\n        if (record == NULL || !record->is_complete || record->is_union ||\n            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||\n            object->initializer_count != record->field_count) {\n            return false;\n        }\n    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        const MinicArrayType *array_type;\n        const MinicRecord *record;\n\n        if (!minic_riscv64_record_array_info(\n                program, object->type, &array_type, &record) ||\n            record->field_count == 0U ||\n            array_type->element_count > SIZE_MAX / record->field_count ||\n            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||\n            object->initializer_count != array_type->element_count * record->field_count) {\n            return false;\n        }\n    } else {\n'''
text = replace_once(text, old, new, "record array backend validation")

old = '''    } else if (minic_type_is_record(object->type)) {\n        if (!minic_riscv64_emit_record_values(file, program, object)) {\n            return false;\n        }\n    } else {\n'''
new = '''    } else if (minic_type_is_record(object->type)) {\n        if (!minic_riscv64_emit_record_values(file, program, object)) {\n            return false;\n        }\n    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        if (!minic_riscv64_emit_record_array_values(file, program, object)) {\n            return false;\n        }\n    } else {\n'''
text = replace_once(text, old, new, "record array backend emission")
path.write_text(text)

print("staged static record arrays with flattened integer-field aggregate initializers")
