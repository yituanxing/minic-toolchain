#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


# Frontend: support one-dimensional static arrays of complete structs whose
# direct fields are integer scalars.  Initializer values are stored flattened
# by [element][field]; the backend reconstructs target layout from record
# field offsets instead of baking RV64 padding into the parser.
replace_once(
    "src/frontend/parser_global.c",
    "#include <stdint.h>\n#include <stdlib.h>\n",
    "#include <limits.h>\n#include <stdint.h>\n#include <stdlib.h>\n",
    "parser_global limits include",
)

path = Path("src/frontend/parser_global.c")
text = path.read_text()
marker = "static bool parse_static_record(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {\n"
helper = r'''static bool parse_static_record_array(MinicParser *parser,
                                      MinicType element_type,
                                      MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicGlobalObjectId object_id;
    MinicType object_type;
    size_t element_count;
    size_t initializer_count;
    bool inferred_bound;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_type_is_record(element_type)) {
        return false;
    }
    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-union struct type");
        return false;
    }
    for (size_t field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_type_is_integer(field->type)) {
            minic_parser_error(parser,
                               "static record array currently requires direct integer scalar fields");
            return false;
        }
    }

    element_count = 0U;
    inferred_bound = false;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type)) {
            minic_parser_error(parser, "cannot create inferred static record array type");
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
               !minic_c0_program_add_array_type(
                   parser->program, element_type, element_count, &object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot create fixed static record array type");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_LBRACE,
                             "expected '{' in static record array initializer")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static record array initializer");
        }
        return false;
    }

    initializer_count = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t field_index;

        if (!inferred_bound && initializer_count >= element_count) {
            minic_parser_error(parser, "too many static record array initializers");
            return false;
        }
        if (!minic_parser_expect(parser,
                                 MINIC_TOKEN_LBRACE,
                                 "expected '{' before static record array element")) {
            return false;
        }
        field_index = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            const MinicRecordField *field;
            int64_t parsed;

            if (field_index >= record->field_count) {
                minic_parser_error(parser, "too many fields in static record array element");
                return false;
            }
            field = minic_c0_record_field(record, field_index);
            if (field == NULL || !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
                return false;
            }
            if (parsed < INT_MIN || parsed > INT_MAX ||
                !minic_c0_global_object_add_initializer(
                    parser->program, object_id, (int)parsed)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "static record array integer initializer is out of range");
                }
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
                minic_parser_error(parser,
                                   "expected ',' or '}' in static record array element");
                return false;
            }
        }
        while (field_index < record->field_count) {
            if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
                minic_parser_error(parser, "cannot zero-fill static record array element");
                return false;
            }
            field_index += 1U;
        }
        if (!minic_parser_expect(parser,
                                 MINIC_TOKEN_RBRACE,
                                 "expected '}' after static record array element")) {
            return false;
        }
        initializer_count += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static record array initializer");
            return false;
        }
    }
    if (initializer_count == 0U ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_RBRACE,
                             "expected '}' after static record array initializer")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "static record array requires at least one initializer");
        }
        return false;
    }
    if (inferred_bound) {
        element_count = initializer_count;
        if (!minic_c0_program_complete_array_type(parser->program, object_type, element_count)) {
            minic_parser_error(parser, "cannot infer static record array bound");
            return false;
        }
    } else {
        while (initializer_count < element_count) {
            for (size_t field_index = 0U; field_index < record->field_count; ++field_index) {
                if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
                    minic_parser_error(parser, "cannot zero-fill static record array tail");
                    return false;
                }
            }
            initializer_count += 1U;
        }
    }
    return minic_parser_expect(parser,
                               MINIC_TOKEN_SEMICOLON,
                               "expected ';' after static record array");
}

'''
if text.count(marker) != 1:
    raise SystemExit("static record parser marker not found uniquely")
text = text.replace(marker, helper + marker, 1)
old = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "static record array globals are not supported");
        return false;
    }
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser, type, name_span);
    }
'''
if text.count(old) != 1:
    raise SystemExit("static record array rejection not found uniquely")
path.write_text(text.replace(old, new, 1))

# Backend: detect array-of-struct globals and emit each direct integer field at
# its target ABI offset, including both inter-field and tail padding.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
marker = "static bool minic_riscv64_emit_global_object(FILE *file,\n"
helper = r'''static bool minic_riscv64_is_record_array(const MinicC0Program *program, MinicType type) {
    const MinicArrayType *array_type;
    const MinicRecord *record;

    if (program == NULL || !minic_type_is_array(type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    if (array_type == NULL || array_type->element_count == 0U ||
        !minic_type_is_record(array_type->element_type)) {
        return false;
    }
    record = minic_c0_program_record(program, array_type->element_type.record_id);
    return record != NULL && record->is_complete && !record->is_union && record->field_count != 0U;
}

static bool minic_riscv64_emit_record_array_values(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicGlobalObject *object) {
    const MinicArrayType *array_type;
    const MinicRecord *record;
    size_t record_size;
    size_t record_alignment;
    size_t expected_values;
    size_t cursor;
    size_t value_index;

    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||
        object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
        !minic_riscv64_is_record_array(program, object->type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(program, object->type.array_type_id);
    record = minic_c0_program_record(program, array_type->element_type.record_id);
    if (array_type == NULL || record == NULL ||
        array_type->element_count > SIZE_MAX / record->field_count) {
        return false;
    }
    expected_values = array_type->element_count * record->field_count;
    if (object->initializer_count != expected_values ||
        !minic_riscv64_type_layout(
            program, array_type->element_type, &record_size, &record_alignment) ||
        record_size == 0U || array_type->element_count > SIZE_MAX / record_size ||
        object->storage_size != array_type->element_count * record_size) {
        return false;
    }
    (void)record_alignment;

    cursor = 0U;
    value_index = 0U;
    for (size_t element_index = 0U; element_index < array_type->element_count; ++element_index) {
        size_t element_base;

        element_base = element_index * record_size;
        for (size_t field_index = 0U; field_index < record->field_count; ++field_index) {
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
            field_offset = element_base + field->storage_offset;
            if (field_offset < cursor || field_offset > object->storage_size ||
                field_size > object->storage_size - field_offset ||
                !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
                return false;
            }
            directive = minic_type_is_char_integer(field->type)    ? ".byte"
                        : minic_type_is_short_integer(field->type) ? ".half"
                        : minic_type_is_long_integer(field->type)  ? ".dword"
                                                                   : ".word";
            value = object->initializer_values[value_index++];
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
        if (cursor > element_base + record_size ||
            !minic_riscv64_emit_zero_bytes(file, element_base + record_size - cursor)) {
            return false;
        }
        cursor = element_base + record_size;
    }
    return cursor == object->storage_size && value_index == object->initializer_count;
}

'''
if text.count(marker) != 1:
    raise SystemExit("global emitter marker not found uniquely")
text = text.replace(marker, helper + marker, 1)

old = '''    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || record->is_union ||
            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
            object->initializer_count != record->field_count) {
            return false;
        }
    } else {
'''
new = '''    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || record->is_union ||
            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
            object->initializer_count != record->field_count) {
            return false;
        }
    } else if (minic_riscv64_is_record_array(program, object->type)) {
        const MinicArrayType *array_type;
        const MinicRecord *record;

        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        record = array_type == NULL
                     ? NULL
                     : minic_c0_program_record(program, array_type->element_type.record_id);
        if (array_type == NULL || record == NULL ||
            array_type->element_count > SIZE_MAX / record->field_count ||
            object->initializer_count != array_type->element_count * record->field_count ||
            object->function_relocation_count != 0U || object->object_relocation_count != 0U) {
            return false;
        }
    } else {
'''
if text.count(old) != 1:
    raise SystemExit(f"record-array validation anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''    } else if (minic_type_is_record(object->type)) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else {
'''
new = '''    } else if (minic_type_is_record(object->type)) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else if (minic_riscv64_is_record_array(program, object->type)) {
        if (!minic_riscv64_emit_record_array_values(file, program, object)) {
            return false;
        }
    } else {
'''
if text.count(old) != 1:
    raise SystemExit(f"record-array emission anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged static record arrays with inferred/fixed bounds and target-layout emission")
