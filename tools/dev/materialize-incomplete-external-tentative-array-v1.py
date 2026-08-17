from pathlib import Path

path = Path('src/frontend/parser_function.c')
text = path.read_text()

anchor = '''static bool parse_visible_external_array(MinicParser *parser,
'''
helper = r'''static bool incomplete_array_declarator_is_tentative(const MinicParser *parser) {
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RBRACKET ||
        !minic_parser_advance(&probe)) {
        return false;
    }
    while (function_identifier_is(&probe, "__attribute__") ||
           function_identifier_is(&probe, "__attribute")) {
        size_t depth;

        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN) {
            return false;
        }
        depth = 0U;
        for (;;) {
            if (probe.current.kind == MINIC_TOKEN_LPAREN) {
                depth += 1U;
            } else if (probe.current.kind == MINIC_TOKEN_RPAREN) {
                if (depth == 0U) {
                    return false;
                }
                depth -= 1U;
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (depth == 0U) {
                break;
            }
        }
    }
    return probe.current.kind == MINIC_TOKEN_SEMICOLON;
}

'''
if text.count(anchor) != 1:
    raise SystemExit('visible external array anchor changed')
text = text.replace(anchor, helper + anchor, 1)

old = r'''    /* Incomplete top-level array definitions still need the legacy bound-inference owner.
     * Keep that special case bounded until initializer semantics owns inferred aggregate shape. */
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser,
                               "incomplete external tentative array is not implemented yet");
            return false;
        }
        if (minic_type_is_record(element_type)) {
'''
new = r'''    /* An incomplete external-linkage array may be a tentative declaration that a later
     * declaration/definition completes. Record that canonical entity now; inferred definitions
     * still stay with their existing initializer-shape owners below. */
    if (incomplete_array_declarator_is_tentative(parser)) {
        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, true, &array_type, &is_array) ||
            !is_array || !minic_type_is_array(array_type) ||
            !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           section_name,
                                                           section_name_capacity,
                                                           section_name_length,
                                                           has_section,
                                                           explicit_alignment)) {
            return false;
        }
        return parse_external_tentative_object(parser,
                                               array_type,
                                               name_span,
                                               section_name,
                                               *section_name_length,
                                               *has_section,
                                               *explicit_alignment,
                                               visibility,
                                               has_visibility);
    }

    /* Incomplete definitions still need their existing bound-inference owner. */
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (minic_type_is_record(element_type)) {
'''
if text.count(old) != 1:
    raise SystemExit(f'incomplete array routing shape changed: {text.count(old)}')
text = text.replace(old, new, 1)

old = r'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        !minic_c0_type_is_complete_object(parser->program, object_type)) {
        if (parser != NULL) {
            minic_parser_error(parser,
                               "external tentative definition requires a complete object type");
        }
        return false;
    }
'''
new = r'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_c0_type_is_complete_object(parser->program, object_type) &&
         !minic_type_is_array(object_type))) {
        if (parser != NULL) {
            minic_parser_error(parser,
                               "external tentative definition requires an object or incomplete array type");
        }
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f'tentative completeness guard changed: {text.count(old)}')
text = text.replace(old, new, 1)

path.write_text(text)
