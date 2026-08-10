#!/usr/bin/env python3
"""One-shot exact-marker migration from pure Parser probes to TokenCursor."""

from pathlib import Path


def replace_one(path: str, old: str, new: str, label: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    file.write_text(text.replace(old, new, 1))


def replace_unique_range(text: str,
                         start_marker: str,
                         end_marker: str,
                         replacement: str,
                         label: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker, start + 1)
    if start < 0 or end < 0:
        raise SystemExit(f"{label}: migration markers missing")
    if text.find(start_marker, start + 1) >= 0:
        raise SystemExit(f"{label}: start marker is not unique")
    return text[:start] + replacement + text[end:]


def update_makefile() -> None:
    path = Path("Makefile")
    text = path.read_text()

    source = "\tsrc/frontend/token_cursor.c \\\n"
    if source not in text:
        anchor = "\tsrc/frontend/token.c \\\n"
        if text.count(anchor) != 1:
            raise SystemExit("token cursor compiler source anchor mismatch")
        text = text.replace(anchor, anchor + source, 1)

    if "TOKEN_CURSOR_TEST_SOURCES" not in text:
        anchor = """LEXER_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/lexer-test

"""
        block = anchor + """TOKEN_CURSOR_TEST_SOURCES := \\
\tsrc/frontend/lexer.c \\
\tsrc/frontend/token.c \\
\tsrc/frontend/token_cursor.c \\
\ttests/frontend/token_cursor_test.c
TOKEN_CURSOR_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(TOKEN_CURSOR_TEST_SOURCES))
TOKEN_CURSOR_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/token-cursor-test

"""
        if text.count(anchor) != 1:
            raise SystemExit("token cursor test model anchor mismatch")
        text = text.replace(anchor, block, 1)

    old = ".PHONY: all help prepare check check-fast check-token-model check-lexer \\\n"
    new = ".PHONY: all help prepare check check-fast check-token-model check-lexer check-token-cursor \\\n"
    if old in text:
        text = text.replace(old, new, 1)
    elif "check-token-cursor" not in text:
        raise SystemExit("token cursor phony anchor mismatch")

    help_anchor = '\t\t"  make check-lexer        Run the C0 lexer unit gate" \\\n'
    help_line = '\t\t"  make check-token-cursor Verify semantic-free lexer lookahead isolation" \\\n'
    if help_line not in text:
        if text.count(help_anchor) != 1:
            raise SystemExit("token cursor help anchor mismatch")
        text = text.replace(help_anchor, help_anchor + help_line, 1)

    if "$(TOKEN_CURSOR_TEST_BINARY):" not in text:
        anchor = """$(LEXER_TEST_BINARY): $(LEXER_TEST_OBJECTS)
\t@mkdir -p "$(dir $@)"
\t$(CC) $(LEXER_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

"""
        block = anchor + """$(TOKEN_CURSOR_TEST_BINARY): $(TOKEN_CURSOR_TEST_OBJECTS)
\t@mkdir -p "$(dir $@)"
\t$(CC) $(TOKEN_CURSOR_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

"""
        if text.count(anchor) != 1:
            raise SystemExit("token cursor link rule anchor mismatch")
        text = text.replace(anchor, block, 1)

    if "check-token-cursor:" not in text:
        anchor = """check-lexer: $(LEXER_TEST_BINARY)
\t"$(abspath $(LEXER_TEST_BINARY))"

"""
        block = anchor + """check-token-cursor: $(TOKEN_CURSOR_TEST_BINARY)
\t"$(abspath $(TOKEN_CURSOR_TEST_BINARY))"

"""
        if text.count(anchor) != 1:
            raise SystemExit("token cursor check target anchor mismatch")
        text = text.replace(anchor, block, 1)

    old_fast = "check-fast: check-token-model check-lexer check-type"
    new_fast = "check-fast: check-token-model check-lexer check-token-cursor check-type"
    if old_fast in text:
        text = text.replace(old_fast, new_fast, 1)
    elif new_fast not in text:
        raise SystemExit("token cursor check-fast anchor mismatch")

    dep_anchor = "-include $(LEXER_TEST_OBJECTS:.o=.d)\n"
    dep_line = "-include $(TOKEN_CURSOR_TEST_OBJECTS:.o=.d)\n"
    if dep_line not in text:
        if text.count(dep_anchor) != 1:
            raise SystemExit("token cursor dependency anchor mismatch")
        text = text.replace(dep_anchor, dep_anchor + dep_line, 1)

    path.write_text(text)


def migrate_function_probes() -> None:
    path = Path("src/frontend/parser_function.c")
    text = path.read_text()
    include = '#include "frontend/token_cursor.h"\n'
    if include not in text:
        anchor = '#include "frontend/parser_internal.h"\n'
        if text.count(anchor) != 1:
            raise SystemExit("parser_function include anchor mismatch")
        text = text.replace(anchor, anchor + include, 1)

    section = r'''bool minic_parser_parse_gnu_section_attribute(
    MinicParser *parser, char *buffer, size_t capacity, size_t *length, bool *has_section) {
    for (;;) {
        MinicTokenCursor probe;
        char parsed[256];
        size_t parsed_length;

        if (parser == NULL || buffer == NULL || length == NULL || has_section == NULL ||
            capacity == 0U) {
            return false;
        }
        if (!section_attribute_token_is(parser, "__attribute__")) {
            return true;
        }

        minic_token_cursor_initialize(&probe, &parser->lexer, parser->current);
        if (!minic_token_cursor_advance(&probe, parser->diagnostic) ||
            probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_token_cursor_advance(&probe, parser->diagnostic) ||
            probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_token_cursor_advance(&probe, parser->diagnostic)) {
            return false;
        }
        if (!minic_token_cursor_text_is(&probe, "section") &&
            !minic_token_cursor_text_is(&probe, "__section__")) {
            return true;
        }

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__") ||
            (!section_attribute_token_is(parser, "section") &&
             !section_attribute_token_is(parser, "__section__")) ||
            !minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after section")) {
            return false;
        }

        parsed_length = 0U;
        if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
            minic_parser_error(parser, "GNU section attribute requires a string literal");
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            size_t cursor;
            size_t end;

            if (parser->current.span.end.offset <= parser->current.span.begin.offset + 1U) {
                minic_parser_error(parser, "invalid GNU section string");
                return false;
            }
            cursor = parser->current.span.begin.offset + 1U;
            end = parser->current.span.end.offset - 1U;
            while (cursor < end) {
                if (parser->source[cursor] == '\\') {
                    minic_parser_error(parser, "escaped GNU section names are not supported yet");
                    return false;
                }
                if (parsed_length + 1U >= sizeof(parsed)) {
                    minic_parser_error(parser, "GNU section name is too long");
                    return false;
                }
                parsed[parsed_length++] = parser->source[cursor++];
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
        if (parsed_length == 0U || parsed_length + 1U > capacity ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after section name") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU section attribute") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU section attribute")) {
            return false;
        }
        parsed[parsed_length] = '\0';
        if (*has_section) {
            if (*length != parsed_length || memcmp(buffer, parsed, parsed_length) != 0) {
                minic_parser_error(parser, "conflicting GNU section attributes");
                return false;
            }
        } else {
            (void)memcpy(buffer, parsed, parsed_length + 1U);
            *length = parsed_length;
            *has_section = true;
        }
    }
}

'''
    text = replace_unique_range(
        text,
        "bool minic_parser_parse_gnu_section_attribute(",
        "bool minic_parser_parse_gnu_function_attributes(MinicParser *parser) {",
        section,
        "section attribute lookahead",
    )

    visibility = r'''static bool parse_gnu_prefix_function_visibility(MinicParser *parser,
                                                 MinicSymbolVisibility *visibility,
                                                 bool *has_visibility) {
    if (parser == NULL || visibility == NULL || has_visibility == NULL) {
        return false;
    }
    *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    *has_visibility = false;
    while (function_identifier_is(parser, "__attribute__")) {
        MinicTokenCursor probe;

        minic_token_cursor_initialize(&probe, &parser->lexer, parser->current);
        if (!minic_token_cursor_advance(&probe, parser->diagnostic) ||
            probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_token_cursor_advance(&probe, parser->diagnostic) ||
            probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_token_cursor_advance(&probe, parser->diagnostic)) {
            return false;
        }
        if (!minic_token_cursor_text_is(&probe, "visibility")) {
            break;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__") ||
            !function_identifier_is(parser, "visibility") || !minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after visibility") ||
            !parse_gnu_visibility_name(parser, visibility) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after visibility") ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in visibility attribute") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected second ')' in visibility attribute")) {
            return false;
        }
        *has_visibility = true;
    }
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool parse_gnu_prefix_function_visibility(",
        "static bool function_signature_matches(const MinicFunction *function,",
        visibility,
        "visibility attribute lookahead",
    )

    visible_array = r'''static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility) {
    MinicTokenCursor probe;
    bool is_declaration;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }

    minic_token_cursor_initialize(&probe, &parser->lexer, parser->current);
    if (!minic_token_cursor_advance(&probe, parser->diagnostic)) {
        return false;
    }
    while (probe.current.kind != MINIC_TOKEN_RBRACKET && probe.current.kind != MINIC_TOKEN_EOF) {
        if (!minic_token_cursor_advance(&probe, parser->diagnostic)) {
            return false;
        }
    }
    if (probe.current.kind != MINIC_TOKEN_RBRACKET ||
        !minic_token_cursor_advance(&probe, parser->diagnostic)) {
        return false;
    }
    is_declaration = probe.current.kind == MINIC_TOKEN_SEMICOLON;

    if (is_declaration) {
        MinicGlobalObjectId object_id;
        MinicType array_type;
        size_t element_count;
        bool incomplete;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        incomplete = parser->current.kind == MINIC_TOKEN_RBRACKET;
        if (incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(
                    parser->program, element_type, &array_type) ||
                !minic_parser_advance(parser)) {
                minic_parser_error(parser, "cannot declare visible incomplete extern array");
                return false;
            }
        } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
                   !minic_c0_program_add_array_type(
                       parser->program, element_type, element_count, &array_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare visible fixed extern array");
            }
            return false;
        }
        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                array_type,
                                                false,
                                                minic_type_is_const(element_type),
                                                &object_id) ||
            !minic_c0_global_object_set_extern(parser->program, object_id) ||
            (has_visibility &&
             !minic_c0_global_object_set_visibility(parser->program, object_id, visibility))) {
            minic_parser_error(parser, "cannot record visible extern array declaration");
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after visible extern array declaration");
    }

    if (!parse_external_integer_array_definition(parser, element_type, name_span)) {
        return false;
    }
    if (has_visibility) {
        MinicGlobalObjectId object_id;

        object_id = minic_parser_find_global_object(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_global_object_set_visibility(parser->program, object_id, visibility)) {
            minic_parser_error(parser, "cannot record visible external array definition");
            return false;
        }
    }
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool parse_visible_external_array(MinicParser *parser,",
        "static bool parse_function(MinicParser *parser, bool is_internal) {",
        visible_array,
        "visible external array lookahead",
    )

    record = r'''static bool record_keyword_starts_standalone_declaration(MinicParser *parser,
                                                          bool *is_standalone) {
    MinicTokenCursor probe;

    if (parser == NULL || is_standalone == NULL ||
        (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
         parser->current.kind != MINIC_TOKEN_KW_UNION)) {
        return false;
    }

    minic_token_cursor_initialize(&probe, &parser->lexer, parser->current);
    if (!minic_token_cursor_advance(&probe, parser->diagnostic)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record tag or definition after record keyword");
        return false;
    }
    if (minic_token_cursor_text_is(&probe, "__attribute__")) {
        *is_standalone = true;
        return true;
    }
    if (!minic_token_cursor_advance(&probe, parser->diagnostic)) {
        return false;
    }
    *is_standalone =
        probe.current.kind == MINIC_TOKEN_SEMICOLON || probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool record_keyword_starts_standalone_declaration(",
        "bool minic_parse_c0_program(const char *path,",
        record,
        "record standalone lookahead",
    )

    path.write_text(text)


def migrate_typedef_probe() -> None:
    path = Path("src/frontend/parser_typedef.c")
    text = path.read_text()
    include = '#include "frontend/token_cursor.h"\n'
    if include not in text:
        anchor = '#include "frontend/parser_internal.h"\n'
        if text.count(anchor) != 1:
            raise SystemExit("parser_typedef include anchor mismatch")
        text = text.replace(anchor, anchor + include, 1)

    replacement = r'''static bool typedef_starts_record_definition(MinicParser *parser, bool *starts_definition) {
    MinicTokenCursor probe;

    if (parser == NULL || starts_definition == NULL ||
        (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
         parser->current.kind != MINIC_TOKEN_KW_UNION)) {
        return false;
    }
    minic_token_cursor_initialize(&probe, &parser->lexer, parser->current);
    if (!minic_token_cursor_advance(&probe, parser->diagnostic)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_IDENTIFIER &&
        !minic_token_cursor_advance(&probe, parser->diagnostic)) {
        return false;
    }
    *starts_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

'''
    text = replace_unique_range(
        text,
        "static bool typedef_starts_record_definition(MinicParser *parser, bool *starts_definition) {",
        "bool minic_parser_parse_typedef(MinicParser *parser) {",
        replacement,
        "typedef record lookahead",
    )
    path.write_text(text)


def main() -> None:
    update_makefile()
    migrate_function_probes()
    migrate_typedef_probe()
    print("semantic-free TokenCursor migration applied")


if __name__ == "__main__":
    main()
