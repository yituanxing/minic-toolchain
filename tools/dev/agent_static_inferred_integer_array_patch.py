from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text()
    if old in text:
        if text.count(old) != 1:
            raise SystemExit(f"{label}: expected one old anchor, found {text.count(old)}")
        path.write_text(text.replace(old, new, 1))
        return
    if new in text:
        return
    raise SystemExit(f"{label}: neither old nor new anchor found")


parser = root / "src/frontend/parser_global.c"
anchor = '''static bool parse_static_inferred_char_array(MinicParser *parser,\n'''
helper = r'''static bool parse_static_inferred_integer_array(MinicParser *parser,
                                                MinicType element_type,
                                                MinicSourceSpan name_span,
                                                char *section_name,
                                                size_t section_capacity,
                                                size_t *section_name_length,
                                                bool *has_section,
                                                size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t initializer_count;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_integer(element_type) || !minic_type_is_const(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static integer array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            true,
                                            &object_id) ||
        (*has_section && !minic_c0_global_object_set_section(parser->program,
                                                             object_id,
                                                             section_name,
                                                             *section_name_length)) ||
        (*explicit_alignment != 0U &&
         !minic_c0_global_object_set_explicit_alignment(
             parser->program, object_id, *explicit_alignment)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in static array initializer")) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin inferred static integer array");
        }
        return false;
    }

    initializer_count = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record inferred static integer initializer");
            }
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
            minic_parser_error(parser, "expected ',' or '}' in inferred static array initializer");
            return false;
        }
    }
    if (initializer_count == 0U ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer") ||
        !minic_c0_program_complete_array_type(parser->program, object_type, initializer_count)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot infer static integer array bound");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after inferred static integer array");
}

'''
text = parser.read_text()
if "parse_static_inferred_integer_array" not in text:
    if anchor not in text:
        raise SystemExit("static inferred array helper anchor not found")
    parser.write_text(text.replace(anchor, helper + anchor, 1))

old_dispatch = r'''    if (minic_type_is_char_integer(element_type)) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            return parse_static_inferred_char_array(parser,
                                                    element_type,
                                                    name_span,
                                                    section_name,
                                                    section_capacity,
                                                    section_name_length,
                                                    has_section,
                                                    explicit_alignment);
        }
    }
'''
new_dispatch = r'''    {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_char_integer(element_type)) {
                return parse_static_inferred_char_array(parser,
                                                        element_type,
                                                        name_span,
                                                        section_name,
                                                        section_capacity,
                                                        section_name_length,
                                                        has_section,
                                                        explicit_alignment);
            }
            return parse_static_inferred_integer_array(parser,
                                                       element_type,
                                                       name_span,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment);
        }
    }
'''
replace_once(parser, old_dispatch, new_dispatch, "static inferred integer dispatch")

focused = root / "tests/compiler/c0/run-foundation-focused.sh"
replace_once(
    focused,
    "    run-static-inferred-char-arrays.sh \\\n",
    "    run-static-inferred-char-arrays.sh \\\n    run-static-inferred-integer-array.sh \\\n",
    "Foundation inferred integer array gate",
)

print("staged static inferred integer array bound completion")
