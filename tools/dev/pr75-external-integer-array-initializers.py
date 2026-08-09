#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
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
    "src/frontend/parser_function.c",
    '#include "frontend/parser_internal.h"\n\n#include <string.h>\n',
    '#include "frontend/parser_internal.h"\n\n#include <limits.h>\n#include <string.h>\n',
    "parser_function limits include",
)

replace_between(
    "src/frontend/parser_function.c",
    "static bool parse_external_char_array_definition(MinicParser *parser,\n",
    "static bool parse_external_object_definition(MinicParser *parser,\n",
    r'''static bool parse_external_integer_array_definition(MinicParser *parser,
                                                        MinicType element_type,
                                                        MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    MinicGlobalObject *object;
    const MinicArrayType *array_type;
    MinicType declared_array_type;
    size_t element_count;
    size_t initializer_count;
    bool inferred_bound;
    bool definition_omits_bound;

    if (parser == NULL || !minic_type_is_integer(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "external array definition requires an integer element type");
        return false;
    }

    element_count = 0U;
    inferred_bound = false;
    definition_omits_bound = false;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        definition_omits_bound = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional external integer arrays are not supported yet");
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if ((inferred_bound &&
             !minic_c0_program_add_incomplete_array_type(
                 parser->program, element_type, &declared_array_type)) ||
            (!inferred_bound &&
             !minic_c0_program_add_array_type(
                 parser->program, element_type, element_count, &declared_array_type)) ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                declared_array_type,
                                                false,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external integer array definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_is_array(object->type)) {
            minic_parser_error(parser, "conflicting external integer array definition");
            return false;
        }
        array_type = minic_c0_program_array_type(parser->program, object->type.array_type_id);
        if (array_type == NULL || !minic_type_equal(array_type->element_type, element_type)) {
            minic_parser_error(parser, "external integer array definition type mismatch");
            return false;
        }
        if (array_type->element_count != 0U) {
            if (!inferred_bound && array_type->element_count != element_count) {
                minic_parser_error(parser, "external integer array bound mismatch");
                return false;
            }
            element_count = array_type->element_count;
            inferred_bound = false;
        } else if (!inferred_bound &&
                   !minic_c0_program_complete_array_type(
                       parser->program, object->type, element_count)) {
            minic_parser_error(parser, "cannot complete external integer array bound");
            return false;
        }
    }

    object = &parser->program->global_objects[object_id];
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t string_count;

        if (!definition_omits_bound || !minic_type_is_char_integer(element_type) ||
            !inferred_bound ||
            !minic_parser_add_string_literal_initializer(parser, object_id, &string_count) ||
            !minic_c0_program_complete_array_type(parser->program, object->type, string_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "unsupported external string array definition");
            }
            return false;
        }
    } else {
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in external array initializer")) {
            return false;
        }
        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            int64_t parsed;

            if (!minic_parser_parse_integer_constant_expression(parser, &parsed)) {
                return false;
            }
            if (parsed < INT_MIN || parsed > INT_MAX) {
                minic_parser_error(parser, "external integer array initializer is out of supported range");
                return false;
            }
            if (!inferred_bound && initializer_count >= element_count) {
                minic_parser_error(parser, "too many external integer array initializers");
                return false;
            }
            if (!minic_c0_global_object_add_initializer(
                    parser->program, object_id, (int)parsed)) {
                minic_parser_error(parser, "cannot record external integer array initializer");
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
                minic_parser_error(parser, "expected ',' or '}' in external array initializer");
                return false;
            }
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after external array initializer")) {
            return false;
        }
        if (initializer_count == 0U) {
            minic_parser_error(parser, "external integer array requires at least one initializer");
            return false;
        }
        if (inferred_bound) {
            element_count = initializer_count;
            if (!minic_c0_program_complete_array_type(
                    parser->program, object->type, element_count)) {
                minic_parser_error(parser, "cannot infer external integer array bound");
                return false;
            }
        }
    }

    object->is_extern = false;
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external array definition");
}

''',
)

replace_once(
    "src/frontend/parser_function.c",
    "return parse_external_char_array_definition(parser, return_type, name_span);",
    "return parse_external_integer_array_definition(parser, return_type, name_span);",
    "external integer array dispatch",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    '''            } else if (fprintf(file,
                               "  %s %d\\n",
                               directive,
                               object->initializer_values[initializer_index]) < 0) {
                return false;
            }
        }
    }
    return fprintf(file, ".size %s, %zu\\n", object->name, object->storage_size) >= 0;
''',
    '''            } else if (fprintf(file,
                               "  %s %d\\n",
                               directive,
                               object->initializer_values[initializer_index]) < 0) {
                return false;
            }
        }
        if (!minic_riscv64_emit_zero_bytes(
                file, object->storage_size - object->initializer_count * scalar_width)) {
            return false;
        }
    }
    return fprintf(file, ".size %s, %zu\\n", object->name, object->storage_size) >= 0;
''',
    "global integer array zero-fill",
)

print("staged explicit/inferred external integer arrays with brace initializers")
