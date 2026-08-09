#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


def replace_between(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker)) if start >= 0 else -1
    if start < 0 or end < 0 or text.find(start_marker, start + 1) >= 0:
        raise SystemExit(f"{path}: cannot uniquely replace region {start_marker!r}")
    target.write_text(text[:start] + replacement + text[end:])


replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_program_add_array_type(MinicC0Program *program,
                                     MinicType element_type,
                                     size_t element_count,
                                     MinicType *array_type);
""",
    """bool minic_c0_program_add_array_type(MinicC0Program *program,
                                     MinicType element_type,
                                     size_t element_count,
                                     MinicType *array_type);
bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,
                                                MinicType element_type,
                                                MinicType *array_type);
bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count);
""",
)

replace_once(
    "src/frontend/ast.c",
    """bool minic_c0_program_add_array_type(MinicC0Program *program,
                                     MinicType element_type,
                                     size_t element_count,
                                     MinicType *array_type) {
    MinicArrayType descriptor;
    MinicArrayTypeId array_type_id;

    if (program == NULL || array_type == NULL || element_count == 0U ||
        minic_type_is_void(element_type) || minic_type_is_function(element_type)) {
        return false;
    }
    if (!minic_grow_array((void **)&program->array_types,
                          &program->array_type_capacity,
                          program->array_type_count,
                          sizeof(*program->array_types))) {
        return false;
    }

    descriptor.element_type = element_type;
    descriptor.element_count = element_count;
    array_type_id = program->array_type_count;
    program->array_types[program->array_type_count] = descriptor;
    program->array_type_count += 1U;
    *array_type = minic_type_array(array_type_id);
    return true;
}
""",
    """static bool minic_c0_program_add_array_descriptor(MinicC0Program *program,
                                                   MinicType element_type,
                                                   size_t element_count,
                                                   MinicType *array_type) {
    MinicArrayType descriptor;
    MinicArrayTypeId array_type_id;

    if (program == NULL || array_type == NULL || minic_type_is_void(element_type) ||
        minic_type_is_function(element_type)) {
        return false;
    }
    if (!minic_grow_array((void **)&program->array_types,
                          &program->array_type_capacity,
                          program->array_type_count,
                          sizeof(*program->array_types))) {
        return false;
    }

    descriptor.element_type = element_type;
    descriptor.element_count = element_count;
    array_type_id = program->array_type_count;
    program->array_types[program->array_type_count] = descriptor;
    program->array_type_count += 1U;
    *array_type = minic_type_array(array_type_id);
    return true;
}

bool minic_c0_program_add_array_type(MinicC0Program *program,
                                     MinicType element_type,
                                     size_t element_count,
                                     MinicType *array_type) {
    return element_count != 0U &&
           minic_c0_program_add_array_descriptor(program, element_type, element_count, array_type);
}

bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,
                                                MinicType element_type,
                                                MinicType *array_type) {
    return minic_c0_program_add_array_descriptor(program, element_type, 0U, array_type);
}

bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count) {
    MinicArrayType *descriptor;

    if (program == NULL || !minic_type_is_array(array_type) || element_count == 0U ||
        array_type.array_type_id >= program->array_type_count) {
        return false;
    }
    descriptor = &program->array_types[array_type.array_type_id];
    if (descriptor->element_count != 0U) {
        return descriptor->element_count == element_count;
    }
    descriptor->element_count = element_count;
    return true;
}
""",
)

replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span);
""",
    """bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span);
bool minic_parser_add_string_literal_initializer(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 size_t *element_count);
""",
)

replace_once(
    "src/frontend/parser_string.c",
    """bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
""",
    """bool minic_parser_add_string_literal_initializer(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 size_t *element_count) {
    MinicParser probe;
    size_t decoded_length;
    size_t total_length;

    if (parser == NULL || element_count == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    probe = *parser;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(&probe, probe.current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length || !minic_parser_advance(&probe)) {
            return false;
        }
        total_length += decoded_length;
    }
    if (total_length == SIZE_MAX) {
        minic_parser_error(parser, "concatenated string literal is too long");
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        MinicSourceSpan literal_span;

        literal_span = parser->current.span;
        if (!add_string_payload(parser, literal_span, object_id) || !minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
        minic_parser_error(parser, "out of memory while terminating string initializer");
        return false;
    }
    *element_count = total_length + 1U;
    return true;
}

bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
""",
)

replace_between(
    "src/frontend/parser_global.c",
    "bool minic_parser_parse_extern_global(MinicParser *parser) {",
    "static bool\nparse_static_pointer_array",
    r'''bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    bool is_array;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_name(parser, &object_type)) {
        return false;
    }
    if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||
        minic_type_is_array(object_type)) {
        minic_parser_error(parser, "unsupported extern object type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern object name");
        return false;
    }
    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    is_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        size_t element_count;
        MinicType array_type;

        is_array = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (!minic_c0_program_add_incomplete_array_type(
                    parser->program, object_type, &array_type) ||
                !minic_parser_advance(parser)) {
                minic_parser_error(parser, "cannot declare incomplete extern array");
                return false;
            }
        } else {
            if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
                !minic_c0_program_add_array_type(
                    parser->program, object_type, element_count, &array_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot declare extern array");
                }
                return false;
            }
        }
        object_type = array_type;
    }

    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            false,
                                            is_array
                                                ? minic_type_is_const(
                                                      parser->program->array_types
                                                          [object_type.array_type_id]
                                                              .element_type)
                                                : minic_type_is_const(object_type),
                                            &object_id) ||
        !minic_c0_global_object_set_extern(parser->program, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot declare extern object");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after extern object declaration");
}

''',
)

# Insert the array-definition helper directly before the scalar external definition.
replace_once(
    "src/frontend/parser_function.c",
    """static bool parse_external_object_definition(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span) {
""",
    r'''static bool parse_external_char_array_definition(MinicParser *parser,
                                                     MinicType element_type,
                                                     MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    MinicGlobalObject *object;
    const MinicArrayType *array_type;
    MinicType declared_array_type;
    size_t element_count;

    if (parser == NULL || !minic_type_is_char_integer(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "unsupported external array definition");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET,
                            "external string array definition requires an inferred bound")) {
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &declared_array_type) ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                declared_array_type,
                                                false,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external array definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_is_array(object->type)) {
            minic_parser_error(parser, "conflicting external array definition");
            return false;
        }
        array_type = minic_c0_program_array_type(parser->program, object->type.array_type_id);
        if (array_type == NULL || array_type->element_count != 0U ||
            !minic_type_equal(array_type->element_type, element_type)) {
            minic_parser_error(parser, "external array definition type mismatch");
            return false;
        }
        object_id = minic_parser_find_global_object(parser, name_span);
    }

    object = &parser->program->global_objects[object_id];
    object->is_extern = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array") ||
        !minic_parser_add_string_literal_initializer(parser, object_id, &element_count) ||
        !minic_c0_program_complete_array_type(parser->program, object->type, element_count)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot complete external string array definition");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external array definition");
}

static bool parse_external_object_definition(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span) {
''',
)

replace_once(
    "src/frontend/parser_function.c",
    """        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        return parse_external_object_definition(parser, return_type, name_span);
""",
    """        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_external_char_array_definition(parser, return_type, name_span);
        }
        return parse_external_object_definition(parser, return_type, name_span);
""",
)

replace_once(
    "src/target/riscv64/layout.c",
    """        object = &program->global_objects[object_index];
        if (!minic_riscv64_type_layout(program, object->type, &storage_size, &alignment)) {
            return false;
        }
""",
    """        object = &program->global_objects[object_index];
        if (object->is_extern && minic_type_is_array(object->type)) {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, object->type.array_type_id);
            if (array_type != NULL && array_type->element_count == 0U) {
                object->storage_size = 0U;
                object->alignment = 0U;
                continue;
            }
        }
        if (!minic_riscv64_type_layout(program, object->type, &storage_size, &alignment)) {
            return false;
        }
""",
)

print("staged incomplete extern array declarations and inferred string definitions")
