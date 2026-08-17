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
     * declaration/definition completes. Reuse an existing compatible canonical array descriptor
     * instead of creating an ownerless descriptor for each `name[];` redeclaration. */
    if (incomplete_array_declarator_is_tentative(parser)) {
        MinicGlobalObjectId existing_id;
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        existing_id = minic_parser_find_global_object_entity(parser, name_span);
        existing = existing_id == MINIC_GLOBAL_OBJECT_INVALID
                       ? NULL
                       : minic_c0_program_global_object(parser->program, existing_id);
        existing_array = existing != NULL && minic_type_is_array(existing->type)
                             ? minic_c0_program_array_type(parser->program,
                                                           existing->type.array_type_id)
                             : NULL;
        if (existing_array != NULL &&
            minic_parser_external_object_types_compatible(
                parser->program, existing_array->element_type, element_type)) {
            if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['") ||
                !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
                return false;
            }
            array_type = existing->type;
            is_array = true;
        } else if (!minic_parser_parse_array_declarator_suffix(
                       parser, element_type, true, &array_type, &is_array) ||
                   !is_array || !minic_type_is_array(array_type)) {
            return false;
        }
        if (!minic_parser_parse_gnu_object_attribute_lists(parser,
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
